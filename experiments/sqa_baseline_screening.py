"""Screen reproducible ECG signal-quality baselines on grouped held-out data."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from sklearn.ensemble import ExtraTreesClassifier, RandomForestClassifier
from sklearn.impute import SimpleImputer
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import average_precision_score, balanced_accuracy_score, roc_auc_score
from sklearn.model_selection import GroupKFold
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler


FEATURES_3 = ["agreement", "hf_power_ratio", "normalized_diff_rms"]
FEATURES_14 = [
    "agreement", "hf_power_ratio", "baseline_power_ratio", "normalized_diff_rms",
    "flatline_ratio", "signal_kurtosis", "spectral_entropy",
    "beat_count_disagreement", "peak_timing_disagreement_ms",
    "qrs_template_correlation", "qrs_amplitude_cv", "rr_outlier_fraction",
    "detected_beat_count", "xqrs_beat_count",
]


def learned_models():
    return {
        "Logistic-3SQI": (FEATURES_3, lambda: make_pipeline(
            SimpleImputer(strategy="median"), StandardScaler(),
            LogisticRegression(max_iter=2000, class_weight="balanced", random_state=42),
        )),
        "Logistic-14SQI": (FEATURES_14, lambda: make_pipeline(
            SimpleImputer(strategy="median"), StandardScaler(),
            LogisticRegression(max_iter=2000, class_weight="balanced", random_state=42),
        )),
        "RandomForest-14SQI": (FEATURES_14, lambda: make_pipeline(
            SimpleImputer(strategy="median"),
            RandomForestClassifier(n_estimators=40, max_depth=5, min_samples_leaf=10,
                                   class_weight="balanced", random_state=42, n_jobs=1),
        )),
        "ExtraTrees-14SQI": (FEATURES_14, lambda: make_pipeline(
            SimpleImputer(strategy="median"),
            ExtraTreesClassifier(n_estimators=40, max_depth=5, min_samples_leaf=10,
                                 class_weight="balanced", random_state=42, n_jobs=1),
        )),
    }


def rule_scores(frame: pd.DataFrame) -> dict[str, np.ndarray]:
    agreement = frame["agreement"].fillna(0).clip(0, 1).to_numpy()
    template = frame["qrs_template_correlation"].fillna(0).clip(0, 1).to_numpy()
    count = frame["detected_beat_count"].fillna(0).to_numpy()
    approximate_hr = count * 6.0  # 10-second windows
    rr_ok = frame["rr_outlier_fraction"].fillna(1).to_numpy() == 0
    physiological = (approximate_hr >= 40) & (approximate_hr <= 180) & rr_ok
    return {
        "Agreement-only": agreement,
        # Reproducible lightweight analogue, not an exact reimplementation of [Orphanidou 2015].
        "Template-rule-inspired": np.where(physiological, template, 0.0),
    }


def selective_threshold(y: np.ndarray, probability: np.ndarray,
                        max_risk: float, min_kept: int) -> float | None:
    best = None
    for threshold in np.unique(probability):
        keep = probability >= threshold
        if keep.sum() < min_kept:
            continue
        risk = float(np.mean(1 - y[keep]))
        if risk <= max_risk and (best is None or keep.sum() > best[0]):
            best = (int(keep.sum()), float(threshold))
    return None if best is None else best[1]


def metrics(name: str, y: np.ndarray, probability: np.ndarray,
            threshold: float | None) -> dict:
    keep = probability >= threshold if threshold is not None else np.zeros(len(y), dtype=bool)
    return {
        "method": name,
        "test_roc_auc": float(roc_auc_score(y, probability)),
        "test_average_precision": float(average_precision_score(y, probability)),
        "test_balanced_accuracy_at_0_5": float(balanced_accuracy_score(y, probability >= .5)),
        "calibrated_threshold": threshold,
        "test_kept": int(keep.sum()),
        "test_coverage": float(np.mean(keep)),
        "test_bad_among_kept": float(np.mean(1 - y[keep])) if keep.any() else None,
    }


def plot_results(rows: list[dict], output: Path) -> None:
    frame = pd.DataFrame(rows).sort_values("test_roc_auc")
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.8))
    axes[0].barh(frame["method"], frame["test_roc_auc"], color="#2563eb")
    axes[0].axvline(.5, color="black", linestyle="--", linewidth=1)
    axes[0].set(xlim=(0, 1), xlabel="Held-out ROC-AUC", title="Ranking quality windows")
    axes[0].grid(axis="x", alpha=.25)
    valid = frame[frame["test_bad_among_kept"].notna()]
    axes[1].scatter(valid["test_coverage"], valid["test_bad_among_kept"], s=70, color="#dc2626")
    offsets = [(4, 8), (4, -12), (4, 16), (4, -20), (4, 24), (4, -28)]
    for offset, (_, row) in zip(offsets, valid.iterrows()):
        axes[1].annotate(row["method"], (row["test_coverage"], row["test_bad_among_kept"]),
                         xytext=offset, textcoords="offset points", fontsize=8)
    axes[1].axhline(.05, color="black", linestyle="--", linewidth=1, label="5% risk limit")
    axes[1].set(xlim=(0, 1), ylim=(0, 1), xlabel="Held-out coverage",
                ylabel="Bad among retained", title="Threshold transfer to held-out groups")
    axes[1].grid(alpha=.25)
    axes[1].legend()
    fig.tight_layout()
    fig.savefig(output, dpi=180, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--windows", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-risk", type=float, default=.05)
    parser.add_argument("--min-kept", type=int, default=30)
    args = parser.parse_args()
    data = pd.read_csv(args.windows)
    data = data[data["rr_target_ok"].notna() & data["agreement"].notna()].copy()
    development = data[data["split"] == "development"].reset_index(drop=True)
    test = data[data["split"] == "test"].reset_index(drop=True)
    y_dev = development["rr_target_ok"].astype(int).to_numpy()
    y_test = test["rr_target_ok"].astype(int).to_numpy()
    rows = []

    dev_rules, test_rules = rule_scores(development), rule_scores(test)
    for name in dev_rules:
        threshold = selective_threshold(y_dev, dev_rules[name], args.max_risk, args.min_kept)
        rows.append(metrics(name, y_test, test_rules[name], threshold))

    for name, (features, factory) in learned_models().items():
        oof = np.zeros(len(development))
        splitter = GroupKFold(n_splits=5)
        for train_index, validation_index in splitter.split(
            development, y_dev, development["source_group"]
        ):
            candidate = factory()
            candidate.fit(development.iloc[train_index][features], y_dev[train_index])
            oof[validation_index] = candidate.predict_proba(
                development.iloc[validation_index][features]
            )[:, 1]
        threshold = selective_threshold(y_dev, oof, args.max_risk, args.min_kept)
        final = factory()
        final.fit(development[features], y_dev)
        probability = final.predict_proba(test[features])[:, 1]
        rows.append(metrics(name, y_test, probability, threshold))

    args.output.mkdir(parents=True, exist_ok=True)
    result = pd.DataFrame(rows).sort_values("test_roc_auc", ascending=False)
    result.to_csv(args.output / "baseline_results.csv", index=False)
    plot_results(rows, args.output / "baseline_comparison.png")
    manifest = {
        "protocol": "sqa-baseline-screening-v1",
        "target": "firmware R/RR usable: no FP/FN, >=2 adjacent RR pairs, RR MAE <=20 ms",
        "window_seconds": 10,
        "development": {"windows": len(development), "source_groups": int(development.source_group.nunique())},
        "test": {"windows": len(test), "source_groups": int(test.source_group.nunique())},
        "threshold_selection": f"5-fold grouped OOF; risk <= {args.max_risk}; min kept {args.min_kept}",
        "results": rows,
    }
    (args.output / "metrics.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(result.to_string(index=False))


if __name__ == "__main__":
    main()
