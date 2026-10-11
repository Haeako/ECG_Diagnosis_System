"""Explain 50 held-out AF ECG windows with exact TreeSHAP and waveforms."""

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
import shap
import wfdb

from reproducibility import RANDOM_SEED


def select_balanced(frame: pd.DataFrame, count: int) -> pd.DataFrame:
    """Deterministically cover TP/TN and both error modes across records."""
    rng = np.random.default_rng(RANDOM_SEED)
    quotas = {"TP": 15, "TN": 15, "FP": 10, "FN": 10}
    if count != 50:
        base = count // 4
        quotas = {key: base for key in quotas}
        for key in list(quotas)[:count - 4 * base]: quotas[key] += 1
    chosen = []
    for outcome, quota in quotas.items():
        group = frame[frame.outcome == outcome]
        if group.empty: continue
        # Round-robin records prevents one long AFDB record dominating examples.
        buckets = {record: values.index.to_list() for record, values in group.groupby("record_id")}
        for values in buckets.values(): rng.shuffle(values)
        records = sorted(buckets)
        while len(chosen) < sum(quotas[k] for k in list(quotas)[:list(quotas).index(outcome)+1]) and records:
            for record in list(records):
                if buckets[record]: chosen.append(buckets[record].pop())
                if not buckets[record]: records.remove(record)
                if len([idx for idx in chosen if frame.loc[idx, "outcome"] == outcome]) >= quota: break
            if len([idx for idx in chosen if frame.loc[idx, "outcome"] == outcome]) >= quota: break
    if len(chosen) < count:
        remaining = frame.drop(index=chosen).sort_values(["record_id", "window_start_sample"])
        chosen.extend(remaining.index[:count-len(chosen)].tolist())
    return frame.loc[chosen[:count]].copy().reset_index(drop=True)


def positive_shap(explainer: shap.TreeExplainer, values: pd.DataFrame) -> tuple[np.ndarray, float]:
    explained = explainer(values)
    shap_values = np.asarray(explained.values)
    base = np.asarray(explained.base_values)
    if shap_values.ndim == 3: shap_values = shap_values[:, :, 1]
    if base.ndim == 2: base = base[:, 1]
    return shap_values, float(np.mean(base))


def explanation_text(row: pd.Series, feature_names: list[str]) -> str:
    contributions = [(name, float(row[f"shap_{name}"])) for name in feature_names]
    support = sorted(contributions, key=lambda item: item[1], reverse=True)[:2]
    oppose = sorted(contributions, key=lambda item: item[1])[:2]
    fmt = lambda items: ", ".join(f"{name} ({value:+.3f})" for name, value in items)
    return f"AF-supporting: {fmt(support)}; AF-opposing: {fmt(oppose)}"


def waveform(record_dir: Path, row: pd.Series) -> tuple[np.ndarray, float, np.ndarray]:
    record = str(row.record_id); samples, meta = wfdb.rdsamp(str(record_dir / record), channels=[0])
    peaks = np.asarray(wfdb.rdann(str(record_dir / record), "qrs").sample, dtype=np.int64)
    index = int(np.argmin(np.abs(peaks - int(row.window_start_sample))))
    end_index = min(index + 30, len(peaks) - 1)
    start, stop = int(peaks[index]), int(peaks[end_index])
    return samples[start:stop+1, 0], float(meta["fs"]), peaks[index:end_index+1] - start


def plot_waveform_pages(samples: pd.DataFrame, record_dir: Path, output: Path) -> None:
    for page_start in range(0, len(samples), 10):
        page = samples.iloc[page_start:page_start+10]
        fig, axes = plt.subplots(5, 2, figsize=(16, 13))
        for axis, (_, row) in zip(axes.flat, page.iterrows()):
            ecg, fs, peaks = waveform(record_dir, row)
            t = np.arange(len(ecg)) / fs
            axis.plot(t, ecg, color="#222222", linewidth=.65)
            axis.scatter(peaks/fs, ecg[peaks], s=8, color="#d62728", zorder=3)
            axis.set_title(f"#{int(row.sample_id):02d} {row.record_id} {row.outcome} | true={int(row.label)} pAF={row.probability:.3f}\n{row.explanation}", fontsize=8)
            axis.set_xlabel("s", fontsize=7); axis.set_ylabel("mV", fontsize=7); axis.tick_params(labelsize=7); axis.grid(alpha=.15)
        for axis in axes.flat[len(page):]: axis.axis("off")
        fig.tight_layout(); fig.savefig(output / f"ecg_examples_{page_start+1:02d}_{page_start+len(page):02d}.png", dpi=160); plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, default=Path("outputs/af_detection/features.csv"))
    parser.add_argument("--experiment", type=Path, default=Path("outputs/af_detection/final_benchmark/experiment.json"))
    parser.add_argument("--model", type=Path, default=Path("outputs/af_detection/final_benchmark/models/extra_trees_selected.pkl"))
    parser.add_argument("--wfdb-dir", type=Path, default=Path("data/afdb"))
    parser.add_argument("--output", type=Path, default=Path("outputs/af_detection/xai_50"))
    parser.add_argument("--count", type=int, default=50)
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    experiment = json.loads(args.experiment.read_text(encoding="utf-8"))
    artifact = pickle.loads(args.model.read_bytes()); model, features = artifact["model"], artifact["features"]
    frame = pd.read_csv(args.dataset, dtype={"record_id": str})
    available = {path.stem for path in args.wfdb_dir.glob("*.dat")}
    frame = frame[frame.record_id.isin(set(experiment["test_records"]) & available)].copy()
    frame["probability"] = model.predict_proba(frame[features])[:, 1]
    frame["prediction"] = (frame.probability >= .5).astype(int)
    frame["outcome"] = np.select(
        [(frame.label == 1)&(frame.prediction == 1), (frame.label == 0)&(frame.prediction == 0),
         (frame.label == 0)&(frame.prediction == 1), (frame.label == 1)&(frame.prediction == 0)],
        ["TP", "TN", "FP", "FN"], default="unknown")
    selected = select_balanced(frame, args.count)
    explainer = shap.TreeExplainer(model, feature_perturbation="tree_path_dependent")
    shap_values, base = positive_shap(explainer, selected[features])
    for index, name in enumerate(features): selected[f"shap_{name}"] = shap_values[:, index]
    selected.insert(0, "sample_id", np.arange(1, len(selected)+1)); selected["explanation"] = selected.apply(explanation_text, axis=1, feature_names=features)
    selected.to_csv(args.output / "samples_with_shap.csv", index=False)
    payload = {"model": str(args.model), "split": "held-out records only", "count": len(selected),
               "features": features, "base_af_probability": base,
               "outcomes": selected.outcome.value_counts().to_dict(),
               "samples": selected.to_dict(orient="records")}
    (args.output/"explanations.json").write_text(json.dumps(payload, indent=2), encoding="utf-8")

    mean_abs = np.mean(np.abs(shap_values), axis=0); order = np.argsort(mean_abs)
    fig, axis = plt.subplots(figsize=(7, 4)); axis.barh(np.asarray(features)[order], mean_abs[order], color="#0072B2")
    axis.set_xlabel("Mean |SHAP| toward AF probability"); axis.set_title("Global explanation — 50 held-out ECG windows")
    fig.tight_layout(); fig.savefig(args.output/"global_shap_importance.png", dpi=180); plt.close(fig)

    fig, axis = plt.subplots(figsize=(8, 5))
    for i, name in enumerate(features):
        axis.scatter(shap_values[:, i], np.full(len(selected), i)+np.linspace(-.15,.15,len(selected)), c=selected[name], cmap="coolwarm", s=18, alpha=.8)
    axis.axvline(0, color="#555", linewidth=.8); axis.set_yticks(range(len(features)), features)
    axis.set_xlabel("SHAP contribution to AF probability"); axis.set_title("Local contributions across selected cases")
    fig.tight_layout(); fig.savefig(args.output/"shap_beeswarm.png", dpi=180); plt.close(fig)
    plot_waveform_pages(selected, args.wfdb_dir, args.output)

    checks = []
    for row in selected.itertuples():
        reconstructed = base + sum(getattr(row, f"shap_{name}") for name in features)
        checks.append(abs(reconstructed-row.probability))
    summary = {"samples": len(selected), "outcomes": selected.outcome.value_counts().to_dict(),
               "records": sorted(selected.record_id.unique().tolist()), "max_shap_additivity_error": max(checks),
               "mean_probability": float(selected.probability.mean())}
    (args.output/"summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__": main()
