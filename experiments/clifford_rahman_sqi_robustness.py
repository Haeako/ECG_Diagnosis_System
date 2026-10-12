"""Evaluate Clifford-style SQI fusion under Rahman-style domain/noise shifts.

This is an application-specific validation: usable means that the production
QRS/RR path has no missed/extra beats and adjacent RR MAE <= 20 ms.  It is not
a reproduction of the clinical-acceptability labels used in either paper.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.impute import SimpleImputer
from sklearn.metrics import balanced_accuracy_score, precision_recall_fscore_support, roc_auc_score
from sklearn.model_selection import GroupKFold
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import RobustScaler
from sklearn.svm import SVC


FEATURES = [
    "clifford_b_sqi", "clifford_p_sqi", "clifford_k_sqi",
    "clifford_s_sqi", "clifford_bas_sqi", "rahman_hos_sqi",
    "rahman_snr_sqi",
]


def estimator():
    return make_pipeline(
        SimpleImputer(strategy="median"), RobustScaler(),
        SVC(C=1.0, kernel="rbf", class_weight="balanced", probability=True, random_state=42),
    )


def metrics(y, probability, threshold=.5):
    prediction = probability >= threshold
    precision, recall, f1, _ = precision_recall_fscore_support(
        y, prediction, average="binary", zero_division=0
    )
    return {
        "windows": int(len(y)), "usable_fraction": float(np.mean(y)),
        "roc_auc": float(roc_auc_score(y, probability)) if len(np.unique(y)) == 2 else None,
        "balanced_accuracy": float(balanced_accuracy_score(y, prediction)),
        "precision": float(precision), "recall": float(recall), "f1": float(f1),
    }


def select_risk_threshold(y, probability, max_risk, min_kept):
    candidates = []
    for threshold in np.unique(probability):
        keep = probability >= threshold
        risk = float(np.mean(1 - y[keep])) if np.any(keep) else None
        if keep.sum() >= min_kept and risk is not None and risk <= max_risk:
            candidates.append((int(keep.sum()), float(threshold), risk))
    return max(candidates) if candidates else None


def static_predictions(frame):
    # Thresholds summarized and stress-tested by Rahman et al.  These rules are
    # deliberately evaluated, not endorsed for deployment.
    return {
        "kSQI_gt_5": frame["clifford_k_sqi"].to_numpy() > 5,
        "pSQI_0.5_to_0.8": frame["clifford_p_sqi"].between(.5, .8).to_numpy(),
        "abs_sSQI_le_0.8": frame["clifford_s_sqi"].abs().to_numpy() <= .8,
        "snrSQI_gt_10": frame["rahman_snr_sqi"].to_numpy() > 10,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-risk", type=float, default=.05)
    parser.add_argument("--min-kept", type=int, default=30)
    args = parser.parse_args()
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("output must be empty or new")
    args.output.mkdir(parents=True, exist_ok=True)

    data = pd.read_csv(args.windows)
    missing = sorted(set(FEATURES + ["split", "source_group", "source_database", "snr", "rr_target_ok"]) - set(data))
    if missing:
        raise ValueError(f"windows CSV lacks fields: {missing}; rerun ecg_quality_emg with current code")
    data = data[data["rr_target_ok"].notna()].copy()
    data["target"] = data["rr_target_ok"].astype(bool).astype(int)
    development = data[data["split"] == "development"].reset_index(drop=True)
    test = data[data["split"] == "test"].reset_index(drop=True)
    y_dev, y_test = development.target.to_numpy(), test.target.to_numpy()

    folds = min(5, development.source_group.nunique())
    oof = np.zeros(len(development))
    for train, valid in GroupKFold(folds).split(development, y_dev, development.source_group):
        model = estimator().fit(development.iloc[train][FEATURES], y_dev[train])
        oof[valid] = model.predict_proba(development.iloc[valid][FEATURES])[:, 1]
    selected = select_risk_threshold(y_dev, oof, args.max_risk, args.min_kept)
    threshold = selected[1] if selected else None
    model = estimator().fit(development[FEATURES], y_dev)
    probability = model.predict_proba(test[FEATURES])[:, 1]

    rows = []
    for name, prediction in static_predictions(test).items():
        precision, recall, f1, _ = precision_recall_fscore_support(
            y_test, prediction, average="binary", zero_division=0
        )
        rows.append({"method": name, "subgroup": "all", "value": "all",
                     "windows": len(test), "balanced_accuracy": balanced_accuracy_score(y_test, prediction),
                     "precision": precision, "recall": recall, "f1": f1})
    for column in ("snr", "source_database"):
        for value, index in test.groupby(column).groups.items():
            subset = test.loc[index]
            stats = metrics(subset.target.to_numpy(), probability[index])
            rows.append({"method": "adaptive_svm_fusion", "subgroup": column,
                         "value": value, **stats})
    pd.DataFrame(rows).to_csv(args.output / "robustness.csv", index=False)

    keep = probability >= threshold if threshold is not None else np.zeros(len(test), dtype=bool)
    test_risk = float(np.mean(1 - y_test[keep])) if np.any(keep) else None
    result = {
        "protocol": "clifford-rahman-sqi-robustness-v1",
        "interpretation": "Clifford-style SQIs/fusion; current PT-XQRS bSQI; QRS/RR usability target",
        "features": FEATURES,
        "development_grouped_oof": {**metrics(y_dev, oof), "groups": int(development.source_group.nunique()),
                                    "risk_threshold": threshold},
        "held_out_test": {**metrics(y_test, probability), "groups": int(test.source_group.nunique()),
                          "kept": int(keep.sum()), "coverage": float(np.mean(keep)), "risk": test_risk},
        "deployment": {"approved": bool(threshold is not None and test_risk is not None
                                           and test_risk <= args.max_risk and keep.sum() >= args.min_kept),
                       "max_risk": args.max_risk, "firmware_exported": False},
        "limitations": ["10-second windows only", "synthetic muscle artefact from one NSTDB channel",
                        "target is QRS/RR usability, not clinician-labelled morphology acceptability"],
    }
    (args.output / "metrics.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
