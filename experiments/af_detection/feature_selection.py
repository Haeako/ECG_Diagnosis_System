"""Record-grouped feature selection and diagnostic plots for AF RR features."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", str(Path("outputs/matplotlib-cache").resolve()))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from sklearn.ensemble import ExtraTreesClassifier
from sklearn.inspection import permutation_importance
from sklearn.metrics import balanced_accuracy_score, roc_auc_score
from sklearn.model_selection import StratifiedGroupKFold

from features import FEATURE_NAMES
from reproducibility import RANDOM_SEED, lock_random_seed


def classifier(seed: int) -> ExtraTreesClassifier:
    return ExtraTreesClassifier(
        n_estimators=100, max_depth=8, min_samples_leaf=2,
        max_features="sqrt", class_weight="balanced",
        random_state=seed, n_jobs=1,
    )


def grouped_importance(frame: pd.DataFrame, folds: int, seed: int, max_validation: int) -> pd.DataFrame:
    splitter = StratifiedGroupKFold(n_splits=folds, shuffle=True, random_state=seed)
    fold_scores = []
    rng = np.random.default_rng(seed)
    for fold, (train_idx, validation_idx) in enumerate(splitter.split(frame, frame.label, frame.record_id)):
        if len(validation_idx) > max_validation:
            validation_idx = rng.choice(validation_idx, max_validation, replace=False)
        model = classifier(seed + fold)
        model.fit(frame.iloc[train_idx][list(FEATURE_NAMES)], frame.iloc[train_idx].label)
        result = permutation_importance(
            model, frame.iloc[validation_idx][list(FEATURE_NAMES)], frame.iloc[validation_idx].label,
            scoring="balanced_accuracy", n_repeats=3, random_state=seed + fold, n_jobs=1,
        )
        fold_scores.append(result.importances_mean)
    scores = np.asarray(fold_scores)
    ranking = pd.DataFrame({
        "feature": FEATURE_NAMES,
        "permutation_importance_mean": scores.mean(axis=0),
        "permutation_importance_std": scores.std(axis=0, ddof=1),
    }).sort_values("permutation_importance_mean", ascending=False, ignore_index=True)
    ranking.insert(0, "rank", np.arange(1, len(ranking) + 1))
    return ranking


def selection_curve(frame: pd.DataFrame, ordered_features: list[str], folds: int, seed: int) -> pd.DataFrame:
    splitter = list(StratifiedGroupKFold(n_splits=folds, shuffle=True, random_state=seed).split(frame, frame.label, frame.record_id))
    rows = []
    for count in range(1, len(ordered_features) + 1):
        selected = ordered_features[:count]
        balanced, auc = [], []
        for fold, (train_idx, validation_idx) in enumerate(splitter):
            model = classifier(seed + fold)
            model.fit(frame.iloc[train_idx][selected], frame.iloc[train_idx].label)
            predictions = model.predict(frame.iloc[validation_idx][selected])
            probabilities = model.predict_proba(frame.iloc[validation_idx][selected])[:, 1]
            balanced.append(balanced_accuracy_score(frame.iloc[validation_idx].label, predictions))
            auc.append(roc_auc_score(frame.iloc[validation_idx].label, probabilities))
        rows.append({
            "feature_count": count, "features": ";".join(selected),
            "balanced_accuracy_mean": np.mean(balanced), "balanced_accuracy_std": np.std(balanced, ddof=1),
            "roc_auc_mean": np.mean(auc), "roc_auc_std": np.std(auc, ddof=1),
        })
    return pd.DataFrame(rows)


def save_plots(frame: pd.DataFrame, ranking: pd.DataFrame, curve: pd.DataFrame, output: Path) -> None:
    plt.style.use("seaborn-v0_8-whitegrid")
    ordered = ranking.iloc[::-1]
    fig, axis = plt.subplots(figsize=(9, 6))
    axis.barh(ordered.feature, ordered.permutation_importance_mean, xerr=ordered.permutation_importance_std, color="#187a75")
    axis.set(title="AF feature importance (record-grouped CV)", xlabel="Decrease in balanced accuracy after permutation")
    fig.tight_layout()
    fig.savefig(output / "feature_importance.png", dpi=180)
    plt.close(fig)

    correlation = frame[list(FEATURE_NAMES)].corr(method="spearman")
    fig, axis = plt.subplots(figsize=(10, 8))
    image = axis.imshow(correlation, vmin=-1, vmax=1, cmap="RdBu_r")
    axis.set_xticks(range(len(FEATURE_NAMES)), FEATURE_NAMES, rotation=65, ha="right", fontsize=7)
    axis.set_yticks(range(len(FEATURE_NAMES)), FEATURE_NAMES, fontsize=7)
    axis.set_title("Spearman correlation of RR features")
    fig.colorbar(image, ax=axis, shrink=0.8, label="Spearman rho")
    fig.tight_layout()
    fig.savefig(output / "feature_correlation.png", dpi=180)
    plt.close(fig)

    fig, axis = plt.subplots(figsize=(8, 5))
    axis.errorbar(curve.feature_count, curve.balanced_accuracy_mean, yerr=curve.balanced_accuracy_std, marker="o", label="Balanced accuracy")
    axis.errorbar(curve.feature_count, curve.roc_auc_mean, yerr=curve.roc_auc_std, marker="s", label="ROC AUC")
    axis.set(xlabel="Number of top-ranked features", ylabel="Grouped CV score", xticks=curve.feature_count)
    axis.legend()
    fig.tight_layout()
    fig.savefig(output / "feature_selection_curve.png", dpi=180)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("outputs/af_detection/feature_selection"))
    parser.add_argument("--folds", type=int, default=5)
    parser.add_argument("--seed", type=int, choices=[RANDOM_SEED], default=RANDOM_SEED,
                        help="Locked experiment seed (42)")
    parser.add_argument("--max-validation", type=int, default=5000)
    args = parser.parse_args()
    lock_random_seed()
    frame = pd.read_csv(args.dataset, dtype={"record_id": str})
    if frame.record_id.nunique() < args.folds:
        raise SystemExit("The number of records must be at least the number of folds")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    ranking = grouped_importance(frame, args.folds, args.seed, args.max_validation)
    curve = selection_curve(frame, ranking.feature.tolist(), args.folds, args.seed)
    ranking.to_csv(args.output_dir / "feature_ranking.csv", index=False)
    curve.to_csv(args.output_dir / "selection_curve.csv", index=False)
    tolerance = curve.balanced_accuracy_mean.max() - 0.005
    chosen = curve[curve.balanced_accuracy_mean >= tolerance].iloc[0]
    summary = {
        "records": int(frame.record_id.nunique()), "windows": len(frame), "folds": args.folds,
        "selection_metric": "balanced_accuracy", "tolerance_from_best": 0.005,
        "selected_feature_count": int(chosen.feature_count),
        "selected_features": chosen.features.split(";"),
        "selected_balanced_accuracy_mean": float(chosen.balanced_accuracy_mean),
        "selected_roc_auc_mean": float(chosen.roc_auc_mean),
        "warning": "Selection CV is not a held-out final test estimate.",
    }
    (args.output_dir / "selection_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    save_plots(frame, ranking, curve, args.output_dir)
    print(ranking.to_string(index=False))
    print(f"\nSelected {summary['selected_feature_count']} features: {', '.join(summary['selected_features'])}")


if __name__ == "__main__":
    main()
