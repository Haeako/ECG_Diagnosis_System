"""Unified, leakage-safe comparison of ECG SQIs and their fusion.

The script deliberately evaluates whether a 10 s window is safe for BPM/RR
use, not whether it looks visually clean. Thresholds and the fusion choice are
made with grouped OOF predictions on development records only.
"""
from __future__ import annotations

import argparse
import json
import pickle
import time
import tracemalloc
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.ensemble import ExtraTreesClassifier
from sklearn.impute import SimpleImputer
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import average_precision_score, balanced_accuracy_score, roc_auc_score
from sklearn.model_selection import GroupKFold
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler


METHODS = {
    # bSQI is the agreement definition in Clifford toolbox bsqi.m. The two
    # detectors here are the project's PT and WFDB XQRS implementations.
    "Clifford-bSQI": ["clifford_b_sqi"],
    # These are low-cost statistical indices described/evaluated by Rahman.
    "Rahman-statistical": [
        "clifford_p_sqi", "clifford_k_sqi", "clifford_s_sqi",
        "clifford_bas_sqi", "rahman_hos_sqi", "rahman_snr_sqi",
    ],
    # Functions/features available in the cloned vital_sqi repository. Column
    # mappings use equivalent local calculations so all methods see one cache.
    "vital_sqi-statistical": [
        "signal_kurtosis", "spectral_entropy", "normalized_diff_rms",
        "hf_power_ratio", "baseline_power_ratio",
    ],
    "Compact-fusion": [
        "clifford_b_sqi", "spectral_entropy", "normalized_diff_rms",
        "qrs_template_correlation", "rr_outlier_fraction",
    ],
    "Full-fusion": [
        "agreement", "hf_power_ratio", "baseline_power_ratio", "normalized_diff_rms",
        "flatline_ratio", "signal_kurtosis", "spectral_entropy",
        "beat_count_disagreement", "peak_timing_disagreement_ms",
        "qrs_template_correlation", "qrs_amplitude_cv", "rr_outlier_fraction",
        "detected_beat_count", "xqrs_beat_count",
    ],
}


def logistic():
    return make_pipeline(SimpleImputer(strategy="median"), StandardScaler(),
                         LogisticRegression(max_iter=2000, class_weight="balanced",
                                            random_state=42))


def extra_trees():
    return make_pipeline(SimpleImputer(strategy="median"),
                         ExtraTreesClassifier(n_estimators=40, max_depth=5,
                                              min_samples_leaf=10,
                                              class_weight="balanced",
                                              random_state=42, n_jobs=1))


def threshold_for_risk(y, p, max_risk=.05, min_kept=30):
    best = None
    for t in np.unique(p):
        keep = p >= t
        if keep.sum() < min_kept:
            continue
        risk = float(np.mean(1 - y[keep]))
        candidate = (int(keep.sum()), -risk, float(t))
        if risk <= max_risk and (best is None or candidate > best):
            best = candidate
    return None if best is None else best[2]


def profile_predict(model, x, repeats=30):
    model.predict_proba(x.iloc[: min(32, len(x))])
    times = []
    tracemalloc.start()
    for _ in range(repeats):
        start = time.perf_counter_ns()
        model.predict_proba(x)
        times.append((time.perf_counter_ns() - start) / len(x) / 1e3)
    _, peak = tracemalloc.get_traced_memory()
    tracemalloc.stop()
    return {
        "python_inference_us_per_window_median": float(np.median(times)),
        "python_inference_us_per_window_p95": float(np.percentile(times, 95)),
        "python_peak_alloc_bytes_batch": int(peak),
        "pickle_model_bytes": len(pickle.dumps(model)),
    }


def evaluate(name, features, factory, dev, test, max_risk, min_kept):
    ydev = dev.rr_target_ok.astype(int).to_numpy()
    ytest = test.rr_target_ok.astype(int).to_numpy()
    oof = np.zeros(len(dev))
    folds = GroupKFold(5)
    for train, valid in folds.split(dev, ydev, dev.source_group):
        model = factory()
        model.fit(dev.iloc[train][features], ydev[train])
        oof[valid] = model.predict_proba(dev.iloc[valid][features])[:, 1]
    threshold = threshold_for_risk(ydev, oof, max_risk, min_kept)
    dev_keep = oof >= threshold if threshold is not None else np.zeros(len(dev), bool)
    model = factory()
    model.fit(dev[features], ydev)
    probability = model.predict_proba(test[features])[:, 1]
    keep = probability >= threshold if threshold is not None else np.zeros(len(test), bool)
    row = {
        "method": name,
        "feature_count": len(features),
        "dev_oof_roc_auc": float(roc_auc_score(ydev, oof)),
        "dev_oof_average_precision": float(average_precision_score(ydev, oof)),
        "threshold": threshold,
        "dev_oof_kept": int(dev_keep.sum()),
        "dev_oof_coverage": float(dev_keep.mean()),
        "dev_oof_bad_among_kept": float(np.mean(1-ydev[dev_keep])) if dev_keep.any() else None,
        "test_roc_auc": float(roc_auc_score(ytest, probability)),
        "test_average_precision": float(average_precision_score(ytest, probability)),
        "test_balanced_accuracy_0_5": float(balanced_accuracy_score(ytest, probability >= .5)),
        "test_kept": int(keep.sum()),
        "test_coverage": float(keep.mean()),
        "test_bad_among_kept": float(np.mean(1-ytest[keep])) if keep.any() else None,
    }
    row.update(profile_predict(model, test[features]))
    return row, model


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--windows", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--max-risk", type=float, default=.05)
    ap.add_argument("--min-kept", type=int, default=30)
    args = ap.parse_args()
    data = pd.read_csv(args.windows)
    data = data[data.rr_target_ok.notna() & data.agreement.notna()].copy()
    dev = data[data.split == "development"].reset_index(drop=True)
    test = data[data.split == "test"].reset_index(drop=True)
    rows, models = [], {}
    for name, features in METHODS.items():
        row, model = evaluate(name, features, logistic, dev, test,
                              args.max_risk, args.min_kept)
        rows.append(row); models[name] = model
    for base in ("Compact-fusion", "Full-fusion"):
        name = base.replace("fusion", "ExtraTrees")
        row, model = evaluate(name, METHODS[base], extra_trees, dev, test,
                              args.max_risk, args.min_kept)
        rows.append(row); models[name] = model

    # This selection uses development data only. Primary objective is maximum
    # risk-controlled coverage; ties: OOF AUC, then fewer features/model bytes.
    eligible = [r for r in rows if r["threshold"] is not None]
    selected = max(eligible, key=lambda r: (r["dev_oof_coverage"], r["dev_oof_roc_auc"],
                   -r["feature_count"], -r["pickle_model_bytes"])) if eligible else max(
                       rows, key=lambda r: (r["dev_oof_roc_auc"], -r["feature_count"]))
    for row in rows:
        row["selected_on_development"] = row["method"] == selected["method"]
        row["deployment_status"] = ("candidate" if row["threshold"] is not None and
                                    row["test_bad_among_kept"] is not None and
                                    row["test_bad_among_kept"] <= args.max_risk else "rejected")

    args.output.mkdir(parents=True, exist_ok=True)
    result = pd.DataFrame(rows).sort_values("dev_oof_roc_auc", ascending=False)
    result.to_csv(args.output / "method_results.csv", index=False)
    cost_cols = ["method", "feature_count", "python_inference_us_per_window_median",
                 "python_inference_us_per_window_p95", "python_peak_alloc_bytes_batch",
                 "pickle_model_bytes"]
    result[cost_cols].to_csv(args.output / "latency_cost.csv", index=False)
    with (args.output / "selected_model.pkl").open("wb") as handle:
        pickle.dump(models[selected["method"]], handle)
    manifest = {
        "protocol": "sqi-unified-bpm-rr-v1",
        "target": "no QRS FP/FN, >=2 adjacent RR pairs, RR MAE <=20 ms",
        "window_seconds": 10, "development_windows": len(dev), "test_windows": len(test),
        "development_groups": int(dev.source_group.nunique()),
        "test_groups": int(test.source_group.nunique()),
        "selection": "5-fold grouped OOF on development; maximize OOF AUC among risk-calibratable methods; ties favor compactness",
        "risk_constraint": args.max_risk, "selected": selected["method"],
        "cost_scope": "Python model inference only; tracemalloc is host allocation, not ESP32 SRAM; common QRS/SQI extraction excluded",
        "results": rows,
    }
    (args.output / "metrics.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    print(result.to_string(index=False))


if __name__ == "__main__":
    main()
