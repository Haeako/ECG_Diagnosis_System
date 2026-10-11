"""Leakage-aware AF model training, held-out evaluation, plots, and report."""

from __future__ import annotations

import argparse
import json
import os
import pickle
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", str(Path("outputs/matplotlib-cache").resolve()))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from sklearn.ensemble import ExtraTreesClassifier, RandomForestClassifier
from sklearn.metrics import (
    accuracy_score, balanced_accuracy_score, confusion_matrix, f1_score,
    precision_score, recall_score, roc_auc_score, roc_curve,
)
from sklearn.model_selection import GroupShuffleSplit
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.linear_model import LogisticRegression
from xgboost import XGBClassifier

from feature_selection import grouped_importance, selection_curve
from features import FEATURE_NAMES
from reproducibility import RANDOM_SEED, lock_random_seed


def models(seed: int, positive_weight: float) -> dict[str, object]:
    common = dict(
        n_estimators=80, max_depth=8, min_samples_leaf=2,
        max_features="sqrt", class_weight="balanced",
        random_state=seed, n_jobs=1,
    )
    return {
        "Logistic Regression": make_pipeline(
            StandardScaler(),
            LogisticRegression(
                class_weight="balanced", max_iter=2000, random_state=seed,
            ),
        ),
        "Random Forest": RandomForestClassifier(**common),
        "Extra Trees": ExtraTreesClassifier(**common),
        "XGBoost": XGBClassifier(
            n_estimators=100, max_depth=4, learning_rate=0.05,
            subsample=0.8, colsample_bytree=0.8, reg_lambda=2.0,
            scale_pos_weight=positive_weight, random_state=seed,
            n_jobs=1, eval_metric="logloss",
        ),
    }


def metric_row(model_name: str, feature_set: str, truth: pd.Series, prediction: np.ndarray,
               probability: np.ndarray, artifact_size: int) -> dict:
    tn, fp, fn, tp = confusion_matrix(truth, prediction, labels=[0, 1]).ravel()
    return {
        "model": model_name, "feature_set": feature_set,
        "accuracy": accuracy_score(truth, prediction),
        "balanced_accuracy": balanced_accuracy_score(truth, prediction),
        "precision": precision_score(truth, prediction, zero_division=0),
        "sensitivity": recall_score(truth, prediction, zero_division=0),
        "specificity": tn / (tn + fp) if tn + fp else 0.0,
        "f1": f1_score(truth, prediction, zero_division=0),
        "roc_auc": roc_auc_score(truth, probability),
        "pickle_bytes": artifact_size,
        "tn": int(tn), "fp": int(fp), "fn": int(fn), "tp": int(tp),
    }


def plot_results(results: pd.DataFrame, predictions: dict, truth: pd.Series, output: Path) -> None:
    plt.style.use("seaborn-v0_8-whitegrid")
    colors = {
        "Logistic Regression": "#6c757d", "Random Forest": "#277da1",
        "Extra Trees": "#43aa8b", "XGBoost": "#f8961e",
    }
    styles = {"selected": "-", "all": "--"}
    fig, axis = plt.subplots(figsize=(8, 6))
    for (model_name, feature_set), values in predictions.items():
        fpr, tpr, _ = roc_curve(truth, values["probability"])
        auc = results[(results.model == model_name) & (results.feature_set == feature_set)].roc_auc.iloc[0]
        axis.plot(fpr, tpr, styles[feature_set], color=colors[model_name], label=f"{model_name}, {feature_set} (AUC={auc:.3f})")
    axis.plot([0, 1], [0, 1], ":", color="#666666")
    axis.set(xlabel="False positive rate", ylabel="True positive rate", title="AF classification on held-out records")
    axis.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(output / "roc_curves.png", dpi=180)
    plt.close(fig)

    fig, axes = plt.subplots(2, 4, figsize=(14, 7))
    for axis, ((model_name, feature_set), values) in zip(axes.flat, predictions.items()):
        matrix = confusion_matrix(truth, values["prediction"], labels=[0, 1])
        axis.imshow(matrix, cmap="Blues")
        for row in range(2):
            for column in range(2):
                axis.text(column, row, f"{matrix[row, column]:,}", ha="center", va="center")
        axis.set(title=f"{model_name} - {feature_set}", xlabel="Predicted", ylabel="True")
        axis.set_xticks([0, 1], ["Non-AF", "AF"])
        axis.set_yticks([0, 1], ["Non-AF", "AF"])
    fig.tight_layout()
    fig.savefig(output / "confusion_matrices.png", dpi=180)
    plt.close(fig)

    labels = results.model + "\n" + results.feature_set
    x = np.arange(len(results))
    width = 0.24
    fig, axis = plt.subplots(figsize=(10, 5))
    for offset, metric in enumerate(("balanced_accuracy", "sensitivity", "specificity")):
        axis.bar(x + (offset - 1) * width, results[metric], width, label=metric.replace("_", " "))
    axis.set_xticks(x, labels, fontsize=8)
    axis.set_ylim(0, 1.05)
    axis.set_ylabel("Held-out score")
    axis.legend()
    fig.tight_layout()
    fig.savefig(output / "model_comparison.png", dpi=180)
    plt.close(fig)


def markdown_report(results: pd.DataFrame, metadata: dict, ranking: pd.DataFrame) -> str:
    ordered = results.sort_values(["balanced_accuracy", "roc_auc"], ascending=False)
    best = ordered.iloc[0]
    lines = [
        "# Báo cáo benchmark mô hình phát hiện AF từ RR/HRV",
        "",
        "## Thiết kế đánh giá",
        "",
        f"- Dataset: MIT-BIH Atrial Fibrillation Database (AFDB), {metadata['windows']:,} cửa sổ từ {metadata['records']} records.",
        f"- Train: {metadata['train_windows']:,} cửa sổ/{len(metadata['train_records'])} records; test: {metadata['test_windows']:,} cửa sổ/{len(metadata['test_records'])} records.",
        "- Feature selection chỉ chạy trên train records bằng 5-fold StratifiedGroupKFold.",
        "- Test records được giữ riêng và chỉ dùng để đánh giá cuối với ngưỡng phân loại 0,5.",
        "- Mỗi cửa sổ gồm 30 RR; cửa sổ giao giữa AF và Non-AF đã bị loại.",
        "",
        "## Feature được chọn",
        "",
        f"Bộ rút gọn gồm **{', '.join(metadata['selected_features'])}**. Bộ này là cấu hình nhỏ nhất có balanced accuracy CV nằm trong 0,5 điểm phần trăm so với giá trị tốt nhất trên train.",
        "",
        "Top permutation importance trên train:",
        "",
        "| Hạng | Feature | Importance trung bình | SD |",
        "|---:|---|---:|---:|",
    ]
    for row in ranking.head(8).itertuples():
        lines.append(f"| {row.rank} | `{row.feature}` | {row.permutation_importance_mean:.4f} | {row.permutation_importance_std:.4f} |")
    lines.extend(["", "## Kết quả holdout", "", "| Mô hình | Feature | Balanced accuracy | Sensitivity | Specificity | F1 | ROC-AUC | Model pickle |", "|---|---|---:|---:|---:|---:|---:|---:|"])
    for row in ordered.itertuples():
        lines.append(f"| {row.model} | {row.feature_set} | {row.balanced_accuracy:.4f} | {row.sensitivity:.4f} | {row.specificity:.4f} | {row.f1:.4f} | {row.roc_auc:.4f} | {row.pickle_bytes / 1024:.1f} KiB |")
    lines.extend([
        "", "## Nhận xét", "",
        f"Cấu hình có balanced accuracy cao nhất trên holdout là **{best.model} ({best.feature_set})**, đạt **{best.balanced_accuracy:.4f}**, sensitivity **{best.sensitivity:.4f}**, specificity **{best.specificity:.4f}** và ROC-AUC **{best.roc_auc:.4f}**.",
        "",
        "Kích thước pickle chỉ dùng so sánh artifact trên máy tính, không phải flash thực tế trên ESP32. Trước triển khai cần xuất cây sang C/C++, đo flash, RAM, latency và kiểm thử bằng RR sinh ra từ chính firmware/R-peak detector.",
        "",
        "Đây là kết quả trên AFDB với QRS annotations có sẵn. Nó chưa đo suy giảm do nhiễu ECG, mất R-peak, quần thể ngoài AFDB hoặc dữ liệu phần cứng AD8232.",
        "",
        "## Hình",
        "",
        "![ROC curves](roc_curves.png)", "", "![Confusion matrices](confusion_matrices.png)", "", "![Model comparison](model_comparison.png)", "",
    ])
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("outputs/af_detection/final_benchmark"))
    parser.add_argument("--test-size", type=float, default=0.25)
    parser.add_argument("--seed", type=int, choices=[RANDOM_SEED], default=RANDOM_SEED,
                        help="Locked experiment seed (42)")
    args = parser.parse_args()
    lock_random_seed()
    frame = pd.read_csv(args.dataset, dtype={"record_id": str})
    splitter = GroupShuffleSplit(n_splits=1, test_size=args.test_size, random_state=args.seed)
    train_idx, test_idx = next(splitter.split(frame, frame.label, frame.record_id))
    train, test = frame.iloc[train_idx].copy(), frame.iloc[test_idx].copy()
    if train.label.nunique() < 2 or test.label.nunique() < 2:
        raise SystemExit("Train/test split lacks one class; select another seed")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    ranking = grouped_importance(train, folds=5, seed=args.seed, max_validation=5000)
    curve = selection_curve(train, ranking.feature.tolist(), folds=5, seed=args.seed)
    tolerance = curve.balanced_accuracy_mean.max() - 0.005
    choice = curve[curve.balanced_accuracy_mean >= tolerance].iloc[0]
    selected = choice.features.split(";")
    ranking.to_csv(args.output_dir / "train_feature_ranking.csv", index=False)
    curve.to_csv(args.output_dir / "train_selection_curve.csv", index=False)

    positive_weight = float((train.label == 0).sum() / (train.label == 1).sum())
    rows, predictions = [], {}
    model_dir = args.output_dir / "models"
    model_dir.mkdir(exist_ok=True)
    for feature_set, columns in (("selected", selected), ("all", list(FEATURE_NAMES))):
        for model_name, model in models(args.seed, positive_weight).items():
            model.fit(train[columns], train.label)
            prediction = model.predict(test[columns])
            probability = model.predict_proba(test[columns])[:, 1]
            artifact = pickle.dumps({"model": model, "features": columns}, protocol=pickle.HIGHEST_PROTOCOL)
            filename = f"{model_name.lower().replace(' ', '_')}_{feature_set}.pkl"
            (model_dir / filename).write_bytes(artifact)
            rows.append(metric_row(model_name, feature_set, test.label, prediction, probability, len(artifact)))
            predictions[(model_name, feature_set)] = {"prediction": prediction, "probability": probability}
    results = pd.DataFrame(rows)
    results.to_csv(args.output_dir / "metrics.csv", index=False)
    metadata = {
        "dataset": str(args.dataset), "seed": args.seed, "records": int(frame.record_id.nunique()), "windows": len(frame),
        "train_records": sorted(train.record_id.unique().tolist()), "test_records": sorted(test.record_id.unique().tolist()),
        "train_windows": len(train), "test_windows": len(test), "selected_features": selected,
    }
    (args.output_dir / "experiment.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    plot_results(results, predictions, test.label, args.output_dir)
    (args.output_dir / "REPORT.md").write_text(markdown_report(results, metadata, ranking), encoding="utf-8")
    print(results.sort_values("balanced_accuracy", ascending=False).to_string(index=False))
    print(f"\nReport: {args.output_dir / 'REPORT.md'}")


if __name__ == "__main__":
    main()
