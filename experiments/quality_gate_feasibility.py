"""Validate a lightweight ECG quality gate before any firmware export."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import joblib
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from sklearn.ensemble import ExtraTreesClassifier
from sklearn.impute import SimpleImputer
from sklearn.metrics import roc_auc_score
from sklearn.model_selection import GroupKFold
from sklearn.pipeline import make_pipeline


FEATURES = [
    "agreement", "hf_power_ratio", "baseline_power_ratio", "normalized_diff_rms",
    "flatline_ratio", "signal_kurtosis", "spectral_entropy",
    "beat_count_disagreement", "peak_timing_disagreement_ms",
    "qrs_template_correlation", "qrs_amplitude_cv", "rr_outlier_fraction",
    "detected_beat_count", "xqrs_beat_count",
]


def model():
    return make_pipeline(
        SimpleImputer(strategy="median"),
        ExtraTreesClassifier(
            n_estimators=40, max_depth=5, min_samples_leaf=10,
            class_weight="balanced", random_state=42, n_jobs=1,
        ),
    )


def risk(y: np.ndarray, keep: np.ndarray) -> float | None:
    return float(np.mean(1 - y[keep])) if np.any(keep) else None


def risk_coverage_curve(y: np.ndarray, probability: np.ndarray) -> pd.DataFrame:
    rows = []
    for threshold in np.linspace(0, 1, 501):
        keep = probability >= threshold
        rows.append({"threshold": threshold, "coverage": float(np.mean(keep)),
                     "risk": risk(y, keep), "kept": int(keep.sum())})
    return pd.DataFrame(rows)


def make_plot(output: Path, calibration_curve: pd.DataFrame, test_curve: pd.DataFrame,
              oof: np.ndarray, y_dev: np.ndarray, probability: np.ndarray,
              y_test: np.ndarray, threshold: float | None, estimator) -> None:
    fig, axes = plt.subplots(1, 3, figsize=(15, 4.6))
    for curve, label, color in ((calibration_curve, "Grouped OOF calibration", "#2563eb"),
                                (test_curve, "Held-out source groups", "#dc2626")):
        valid = curve["risk"].notna()
        axes[0].plot(curve.loc[valid, "coverage"], curve.loc[valid, "risk"],
                     label=label, color=color, linewidth=2)
    axes[0].axhline(.05, color="black", linestyle="--", linewidth=1, label="5% risk limit")
    axes[0].set(xlabel="Coverage (fraction retained)", ylabel="Bad among retained (risk)",
                title="Selective risk versus coverage", xlim=(0, 1), ylim=(0, 1))
    axes[0].grid(alpha=.25)
    axes[0].legend(fontsize=8)

    bins = np.linspace(0, 1, 31)
    axes[1].hist(oof[y_dev == 1], bins=bins, density=True, alpha=.45, color="#16a34a",
                 label="Calibration: usable")
    axes[1].hist(oof[y_dev == 0], bins=bins, density=True, alpha=.45, color="#f59e0b",
                 label="Calibration: bad")
    axes[1].hist(probability[y_test == 1], bins=bins, density=True, histtype="step",
                 linewidth=2, color="#15803d", label="Test: usable")
    axes[1].hist(probability[y_test == 0], bins=bins, density=True, histtype="step",
                 linewidth=2, color="#b91c1c", label="Test: bad")
    if threshold is not None:
        axes[1].axvline(threshold, color="black", linestyle="--", linewidth=1,
                        label=f"Threshold {threshold:.3f}")
    axes[1].set(xlabel="Predicted usability probability", ylabel="Density",
                title="Quality-score distributions", xlim=(0, 1))
    axes[1].grid(alpha=.25)
    axes[1].legend(fontsize=7)

    forest = estimator.named_steps["extratreesclassifier"]
    order = np.argsort(forest.feature_importances_)[-10:]
    axes[2].barh(np.asarray(FEATURES)[order], forest.feature_importances_[order], color="#475569")
    axes[2].set(xlabel="Impurity importance", title="Top quality features")
    axes[2].grid(axis="x", alpha=.25)
    fig.suptitle("ECG quality-gate deployment check", fontsize=14)
    fig.tight_layout()
    fig.savefig(output / "deployment_feasibility.png", dpi=180, bbox_inches="tight")
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

    oof = np.zeros(len(development))
    folds = GroupKFold(n_splits=5)
    for train_index, calibration_index in folds.split(
        development, y_dev, development["source_group"]
    ):
        candidate = model()
        candidate.fit(development.iloc[train_index][FEATURES], y_dev[train_index])
        oof[calibration_index] = candidate.predict_proba(
            development.iloc[calibration_index][FEATURES]
        )[:, 1]

    eligible = []
    for threshold in np.unique(oof):
        keep = oof >= threshold
        candidate_risk = risk(y_dev, keep)
        if keep.sum() >= args.min_kept and candidate_risk is not None and candidate_risk <= args.max_risk:
            eligible.append((int(keep.sum()), float(threshold)))
    threshold = max(eligible)[1] if eligible else None

    final_model = model()
    final_model.fit(development[FEATURES], y_dev)
    probability = final_model.predict_proba(test[FEATURES])[:, 1]
    calibration_curve = risk_coverage_curve(y_dev, oof)
    test_curve = risk_coverage_curve(y_test, probability)
    keep = probability >= threshold if threshold is not None else np.zeros(len(test), dtype=bool)
    test_risk = risk(y_test, keep)
    deployable = bool(
        threshold is not None and keep.sum() >= args.min_kept
        and test_risk is not None and test_risk <= args.max_risk
    )
    result = {
        "protocol": "quality-gate-feasibility-v1",
        "features": FEATURES,
        "model": {"type": "ExtraTreesClassifier", "trees": 40, "max_depth": 5,
                  "min_samples_leaf": 10},
        "calibration": {
            "method": "5-fold grouped out-of-fold",
            "groups": int(development["source_group"].nunique()),
            "windows": len(development), "threshold": threshold,
            "coverage": float(np.mean(oof >= threshold)) if threshold is not None else 0.0,
            "risk": risk(y_dev, oof >= threshold) if threshold is not None else None,
        },
        "held_out_test": {
            "groups": int(test["source_group"].nunique()), "windows": len(test),
            "roc_auc": float(roc_auc_score(y_test, probability)),
            "kept": int(keep.sum()), "coverage": float(np.mean(keep)), "risk": test_risk,
        },
        "deployment": {
            "approved": deployable, "max_allowed_risk": args.max_risk,
            "reason": "held_out_gate_passed" if deployable else "held_out_selective_risk_exceeded",
            "firmware_exported": False,
        },
    }
    args.output.mkdir(parents=True, exist_ok=True)
    calibration_curve.assign(split="grouped_oof").to_csv(
        args.output / "risk_coverage_calibration.csv", index=False)
    test_curve.assign(split="held_out_test").to_csv(
        args.output / "risk_coverage_test.csv", index=False)
    make_plot(args.output, calibration_curve, test_curve, oof, y_dev, probability,
              y_test, threshold, final_model)
    (args.output / "metrics.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    joblib.dump({"model": final_model, "features": FEATURES, "threshold": threshold},
                args.output / "candidate_host_only.pkl")
    report = f"""# ECG quality-gate deployment feasibility

- Candidate: Extra Trees, 40 trees, depth 5, 14 SQIs.
- Grouped OOF threshold: `{threshold}`.
- Held-out ROC-AUC: **{result['held_out_test']['roc_auc']:.4f}**.
- Held-out coverage: **{result['held_out_test']['coverage']:.4f}** ({int(keep.sum())}/{len(test)}).
- Held-out bad-among-kept risk: **{test_risk:.4f}**.
- Required risk: **<= {args.max_risk:.4f}**.
- Firmware export: **{'approved' if deployable else 'rejected'}**.

The host artifact is retained for audit only. A rejected candidate must not be used
to suppress or authorize AF predictions on the ESP32.
"""
    (args.output / "REPORT.md").write_text(report, encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
