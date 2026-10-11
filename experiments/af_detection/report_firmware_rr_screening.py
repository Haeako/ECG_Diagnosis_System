"""Compare annotation-RR and firmware-RR screening on the same AFDB records."""

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
from sklearn.metrics import (
    accuracy_score, balanced_accuracy_score, confusion_matrix, f1_score,
    precision_score, recall_score, roc_auc_score,
)

from export_int8_c import export_model, infer, quant_params, quantize


def evaluate(frame: pd.DataFrame, model, features: list[str]) -> dict:
    prediction = model.predict(frame[features])
    probability = model.predict_proba(frame[features])[:, 1]
    tn, fp, fn, tp = confusion_matrix(frame.label, prediction, labels=[0, 1]).ravel()
    return {
        "windows": len(frame), "accuracy": accuracy_score(frame.label, prediction),
        "balanced_accuracy": (balanced_accuracy_score(frame.label, prediction)
                              if frame.label.nunique() == 2 else None),
        "precision": precision_score(frame.label, prediction, zero_division=0),
        "sensitivity": recall_score(frame.label, prediction, zero_division=0),
        "specificity": tn / (tn + fp) if tn + fp else 0.0,
        "f1": f1_score(frame.label, prediction, zero_division=0),
        "roc_auc": roc_auc_score(frame.label, probability) if frame.label.nunique() == 2 else None,
        "tn": int(tn), "fp": int(fp), "fn": int(fn), "tp": int(tp),
    }


def evaluate_int8(frame: pd.DataFrame, features: list[str], scales: np.ndarray,
                  zero_points: np.ndarray, nodes: list[tuple], roots: list[int]) -> dict:
    values = quantize(frame[features].to_numpy(), scales, zero_points)
    scores, _ = infer(nodes, roots, values)
    prediction = scores >= len(roots) * 128
    probability = scores.astype(np.float64) / (255.0 * len(roots))
    tn, fp, fn, tp = confusion_matrix(frame.label, prediction, labels=[0, 1]).ravel()
    return {
        "windows": len(frame), "accuracy": accuracy_score(frame.label, prediction),
        "balanced_accuracy": balanced_accuracy_score(frame.label, prediction),
        "precision": precision_score(frame.label, prediction, zero_division=0),
        "sensitivity": recall_score(frame.label, prediction, zero_division=0),
        "specificity": tn / (tn + fp) if tn + fp else 0.0,
        "f1": f1_score(frame.label, prediction, zero_division=0),
        "roc_auc": roc_auc_score(frame.label, probability),
        "tn": int(tn), "fp": int(fp), "fn": int(fn), "tp": int(tp),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--annotation-dataset", type=Path, required=True)
    parser.add_argument("--firmware-dataset", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--experiment", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    processed = manifest["processed_records"]
    annotation = pd.read_csv(args.annotation_dataset, dtype={"record_id": str})
    all_annotation = annotation
    annotation = all_annotation[all_annotation.record_id.isin(processed)].copy()
    firmware = pd.read_csv(args.firmware_dataset, dtype={"record_id": str})
    payload = pickle.loads(args.model.read_bytes())
    model, features = payload["model"], payload["features"]
    experiment = json.loads(args.experiment.read_text(encoding="utf-8"))
    calibration = all_annotation[all_annotation.record_id.isin(experiment["train_records"])]
    scales, zero_points = quant_params(calibration, features)
    nodes, roots = export_model(model, features, scales, zero_points)

    aggregate = {
        "annotation_rr": evaluate(annotation, model, features),
        "firmware_rr": evaluate(firmware, model, features),
        "annotation_rr_int8": evaluate_int8(annotation, features, scales, zero_points, nodes, roots),
        "firmware_rr_int8": evaluate_int8(firmware, features, scales, zero_points, nodes, roots),
    }
    aggregate["delta_firmware_minus_annotation"] = {
        key: aggregate["firmware_rr"][key] - aggregate["annotation_rr"][key]
        for key in ("balanced_accuracy", "sensitivity", "specificity", "precision", "f1", "roc_auc")
    }
    per_record = []
    rpeak = {row["record_id"]: row for row in manifest["record_metrics"]}
    for record in processed:
        ideal = evaluate(annotation[annotation.record_id == record], model, features)
        detected = evaluate(firmware[firmware.record_id == record], model, features)
        ideal_ba = ideal["balanced_accuracy"]
        detected_ba = detected["balanced_accuracy"]
        per_record.append({
            "record_id": record,
            "annotation_windows": ideal["windows"],
            "firmware_windows": detected["windows"],
            "annotation_non_af": int((annotation[annotation.record_id == record].label == 0).sum()),
            "annotation_af": int((annotation[annotation.record_id == record].label == 1).sum()),
            "firmware_non_af": int((firmware[firmware.record_id == record].label == 0).sum()),
            "firmware_af": int((firmware[firmware.record_id == record].label == 1).sum()),
            "annotation_accuracy": ideal["accuracy"],
            "firmware_accuracy": detected["accuracy"],
            "annotation_balanced_accuracy": ideal_ba,
            "firmware_balanced_accuracy": detected_ba,
            "balanced_accuracy_delta": (detected_ba - ideal_ba
                                         if detected_ba is not None and ideal_ba is not None else None),
            "annotation_f1": ideal["f1"], "firmware_f1": detected["f1"],
            "rpeak_sensitivity": rpeak[record]["sensitivity"],
            "rpeak_ppv": rpeak[record]["ppv"],
        })
    per_record_frame = pd.DataFrame(per_record)

    pooled_tp = sum(row["tp"] for row in manifest["record_metrics"])
    pooled_fp = sum(row["fp"] for row in manifest["record_metrics"])
    pooled_fn = sum(row["fn"] for row in manifest["record_metrics"])
    rpeak_summary = {
        "tp": pooled_tp, "fp": pooled_fp, "fn": pooled_fn,
        "sensitivity": pooled_tp / (pooled_tp + pooled_fn),
        "ppv": pooled_tp / (pooled_tp + pooled_fp),
    }
    result = {
        "cohort": {"requested_records": manifest["requested_records"],
                   "processed_records": processed,
                   "excluded_records": manifest["excluded_records"]},
        "features": features, "fit_called": False,
        "classification": aggregate, "rpeak_detection": rpeak_summary,
    }
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "screening_metrics.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8"
    )
    per_record_frame.to_csv(args.output_dir / "per_record_screening.csv", index=False)

    metrics = ["balanced_accuracy", "sensitivity", "specificity", "precision", "f1", "roc_auc"]
    x = range(len(metrics))
    fig, axis = plt.subplots(figsize=(9, 5))
    axis.bar([i - 0.2 for i in x], [aggregate["annotation_rr"][m] for m in metrics], 0.4,
             label="Annotation RR", color="#277da1")
    axis.bar([i + 0.2 for i in x], [aggregate["firmware_rr"][m] for m in metrics], 0.4,
             label="Firmware RR", color="#f8961e")
    axis.set_xticks(list(x), [m.replace("_", " ") for m in metrics], rotation=25, ha="right")
    axis.set_ylim(0, 1.05)
    axis.set_ylabel("Score")
    axis.legend()
    fig.tight_layout()
    fig.savefig(args.output_dir / "annotation_vs_firmware_rr.png", dpi=180)
    plt.close(fig)

    lines = [
        "# AF screening report: annotation RR vs firmware RR", "",
        "## Protocol", "",
        f"- Frozen model: `{args.model}`; no fitting or threshold tuning was performed.",
        f"- Features: {', '.join(f'`{name}`' for name in features)}.",
        f"- Requested holdout: {len(manifest['requested_records'])} records; waveform screening: {len(processed)} records.",
        "- Record `00735` was excluded because AFDB does not publish its waveform; the original split was not changed.",
        "- Both columns below are recomputed on the same six waveform-available records.", "",
        "## Classification results", "",
        "| Metric | Annotation RR | Firmware RR | Delta |", "|---|---:|---:|---:|",
    ]
    for metric in metrics:
        lines.append(f"| {metric.replace('_', ' ').title()} | {aggregate['annotation_rr'][metric]:.4f} | {aggregate['firmware_rr'][metric]:.4f} | {aggregate['delta_firmware_minus_annotation'][metric]:+.4f} |")
    lines.extend([
        "", f"Firmware-RR confusion matrix: TN={aggregate['firmware_rr']['tn']:,}, FP={aggregate['firmware_rr']['fp']:,}, FN={aggregate['firmware_rr']['fn']:,}, TP={aggregate['firmware_rr']['tp']:,}.",
        "", "![Annotation RR versus firmware RR](annotation_vs_firmware_rr.png)", "",
        "## INT8 deployment model", "",
        "| Input | Balanced accuracy | Sensitivity | Specificity | F1 | ROC-AUC |", "|---|---:|---:|---:|---:|---:|",
        f"| Annotation RR, INT8 | {aggregate['annotation_rr_int8']['balanced_accuracy']:.4f} | {aggregate['annotation_rr_int8']['sensitivity']:.4f} | {aggregate['annotation_rr_int8']['specificity']:.4f} | {aggregate['annotation_rr_int8']['f1']:.4f} | {aggregate['annotation_rr_int8']['roc_auc']:.4f} |",
        f"| Firmware RR, INT8 | {aggregate['firmware_rr_int8']['balanced_accuracy']:.4f} | {aggregate['firmware_rr_int8']['sensitivity']:.4f} | {aggregate['firmware_rr_int8']['specificity']:.4f} | {aggregate['firmware_rr_int8']['f1']:.4f} | {aggregate['firmware_rr_int8']['roc_auc']:.4f} |",
        "", "INT8 calibration uses only the frozen training records. No calibration or threshold fitting uses the six waveform-screening records.", "",
        "## R-peak detector", "",
        f"Pooled against AFDB `.qrs`: sensitivity **{rpeak_summary['sensitivity']:.4f}**, PPV **{rpeak_summary['ppv']:.4f}** (TP={pooled_tp:,}, FP={pooled_fp:,}, FN={pooled_fn:,}).",
        "", "| Record | R-peak Se | R-peak PPV | Ann. N/AF | FW N/AF | Ann. acc. | FW acc. |", "|---|---:|---:|---:|---:|---:|---:|",
    ])
    for row in per_record_frame.itertuples():
        lines.append(f"| {row.record_id} | {row.rpeak_sensitivity:.4f} | {row.rpeak_ppv:.4f} | {row.annotation_non_af}/{row.annotation_af} | {row.firmware_non_af}/{row.firmware_af} | {row.annotation_accuracy:.4f} | {row.firmware_accuracy:.4f} |")
    lines.extend([
        "", "## Interpretation", "",
        "The performance drop measures the combined effect of waveform-to-ADC simulation and firmware R-peak errors on RR features. It is not evidence that the frozen classifier alone changed.",
        "", "## Limitations", "",
        "- AFDB `.qrs` annotations are automated and unaudited, so R-peak scores are development evidence rather than a manually adjudicated clinical reference.",
        "- The AD8232 front end is simulated using fixed 0.5 Hz AC coupling, 200 ADC counts/mV, midpoint 2048, and 12-bit clipping; it has not been identified from the physical board.",
        "- Channel 0 is used consistently. Alternative lead selection was not tuned on the holdout.",
        "- Annotation-RR and firmware-RR windows are cohort-matched but not one-to-one paired because missed and extra peaks change window boundaries.",
        "- This is AF screening research, not a clinical diagnostic validation.", "",
    ])
    (args.output_dir / "REPORT.md").write_text("\n".join(lines), encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
