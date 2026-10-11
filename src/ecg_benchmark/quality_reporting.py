"""Auditable tables, cluster intervals and plots for the quality benchmark."""
from __future__ import annotations

import csv
import json
from pathlib import Path

import numpy as np
from scipy.stats import rankdata, spearmanr


def clean_json(value):
    if isinstance(value, dict):
        return {str(k): clean_json(v) for k, v in value.items()}
    if isinstance(value, (list, tuple, np.ndarray)):
        return [clean_json(v) for v in value]
    if isinstance(value, Path):
        return str(value.resolve())
    if isinstance(value, np.generic):
        return clean_json(value.item())
    if isinstance(value, float) and not np.isfinite(value):
        return None
    return value


def write_json(path: Path, value) -> None:
    path.write_text(json.dumps(clean_json(value), ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")


def write_csv(path: Path, rows: list[dict]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        fields = sorted({k for row in rows for k in row})
        if not fields:
            stream.write("status\n")
            return
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({k: json.dumps(clean_json(v), ensure_ascii=False) if isinstance(v, (dict, list, tuple))
                             else clean_json(v) for k, v in row.items()})


def sufficient_stats(rows: list[dict]) -> np.ndarray:
    # n, evaluable, good, kept, bad-kept, rejected-good, tp/fp/fn,
    # kept tp/fp/fn, RR error/count, kept RR error/count, insufficient.
    totals = np.zeros(17, dtype=float)
    for row in rows:
        known = row.get("rr_target_ok") is not None
        good = known and bool(row["rr_target_ok"])
        kept = row.get("status") == "usable"
        totals[:6] += [1, known, good, kept and known, kept and known and not good, good and not kept]
        if known:
            values = [row["tp"], row["fp"], row["fn"]]
            totals[6:9] += values
            totals[9:12] += np.asarray(values) * kept
            totals[12:14] += [row["rr_error_sum_ms"], row["rr_pair_count"]]
            totals[14:16] += np.asarray([row["rr_error_sum_ms"], row["rr_pair_count"]]) * kept
        totals[16] += row.get("status") == "insufficient_data"
    return totals


def stats_metrics(s: np.ndarray) -> dict:
    def ratio(a, b):
        return float(a/b) if b else None
    return {
        "windows": int(s[0]), "evaluable": int(s[1]), "target_good": int(s[2]),
        "retained": int(s[3]), "insufficient": int(s[16]),
        "coverage": ratio(s[3], s[1]), "target_good_fraction": ratio(s[2], s[1]),
        "false_accept_among_kept": ratio(s[4], s[3]),
        "false_accept_among_bad": ratio(s[4], s[1]-s[2]),
        "false_reject_among_good": ratio(s[5], s[2]),
        "precision_before": ratio(s[6], s[6]+s[7]), "recall_before": ratio(s[6], s[6]+s[8]),
        "f1_before": ratio(2*s[6], 2*s[6]+s[7]+s[8]),
        "precision_after": ratio(s[9], s[9]+s[10]), "recall_after": ratio(s[9], s[9]+s[11]),
        "f1_after": ratio(2*s[9], 2*s[9]+s[10]+s[11]),
        "rr_mae_before_ms": ratio(s[12], s[13]), "rr_mae_after_ms": ratio(s[14], s[15]),
        "rr_pairs_before": int(s[13]), "rr_pairs_after": int(s[15]),
    }


def summarize(rows: list[dict], seed: int, bootstrap: int = 1000) -> dict:
    groups = sorted({r["source_group"] for r in rows})
    values = np.asarray([sufficient_stats([r for r in rows if r["source_group"] == g]) for g in groups])
    point = stats_metrics(values.sum(axis=0) if len(values) else np.zeros(17))
    point["source_groups"] = len(groups)
    intervals = {}
    if len(groups) >= 2 and bootstrap > 0:
        rng = np.random.default_rng(seed)
        draws = values[rng.integers(0, len(groups), size=(bootstrap, len(groups)))].sum(axis=1)
        metrics = [stats_metrics(draw) for draw in draws]
        for key in ("coverage", "false_accept_among_kept", "false_accept_among_bad", "false_reject_among_good",
                    "target_good_fraction", "f1_before", "f1_after", "rr_mae_before_ms", "rr_mae_after_ms"):
            available = [m[key] for m in metrics if m[key] is not None]
            intervals[key] = {"low": float(np.percentile(available, 2.5)) if available else None,
                              "high": float(np.percentile(available, 97.5)) if available else None,
                              "defined_replicates": len(available)}
    point["cluster_ci95"] = intervals
    return point


def feature_diagnostics(rows: list[dict]) -> list[dict]:
    result = []
    for partition in ("development", "test"):
        for feature, direction in (("agreement", 1), ("hf_power_ratio", -1), ("normalized_diff_rms", -1)):
            selected = [r for r in rows if r["split"] == partition and r.get("rr_target_ok") is not None
                        and r.get(feature) is not None and np.isfinite(r[feature])]
            labels = np.asarray([r["rr_target_ok"] for r in selected], dtype=bool)
            scores = direction * np.asarray([r[feature] for r in selected], dtype=float)
            pos, neg = int(labels.sum()), int((~labels).sum())
            auc = float((rankdata(scores)[labels].sum() - pos*(pos+1)/2) / (pos*neg)) if pos and neg else None
            corr_rows = [r for r in selected if r["rr_mae_ms"] is not None]
            a = [r[feature] for r in corr_rows]
            b = [r["rr_mae_ms"] for r in corr_rows]
            corr = float(spearmanr(a, b).statistic) if len(set(a)) > 1 and len(set(b)) > 1 else None
            result.append({"split": partition, "feature": feature, "usable_direction": direction,
                           "windows": len(selected), "auc_good_target": auc, "spearman_rr_mae": corr,
                           "note": "descriptive; repeated windows/noise are not independent patients"})
    return result


def make_plots(output: Path, rows: list[dict], curve: list[dict], by_snr: list[dict], examples: list[dict]) -> None:
    import matplotlib
    matplotlib.use("Agg")
    from matplotlib import pyplot as plt
    labels = ["original", "24", "18", "12", "6", "0", "-6"]
    fig, axes = plt.subplots(1, 2, figsize=(11, 4))
    for split in ("development", "test"):
        selected = {str(r["snr"]): r for r in by_snr if r["split"] == split and r["subgroup"] == "all"}
        axes[0].plot(labels, [selected.get(s, {}).get("f1_before") for s in labels], marker="o", label=split)
        axes[1].plot(labels, [selected.get(s, {}).get("rr_mae_before_ms") for s in labels], marker="o", label=split)
    for ax in axes:
        ax.set_xlabel("Injected SNR (dB); original = no added noise")
        ax.grid(alpha=.2)
        ax.legend()
    axes[0].set_ylabel("R detection F1")
    axes[1].set_ylabel("Adjacent matched RR MAE (ms)")
    fig.tight_layout()
    fig.savefig(output / "snr_errors.png", dpi=160)
    plt.close(fig)
    fig, axes = plt.subplots(1, 2, figsize=(11, 4))
    axes[0].plot([c["coverage"] for c in curve], [c["false_accept_among_kept"] for c in curve], color="black")
    axes[0].axhline(.05, color="gray", linestyle="--", label="development target 5%")
    axes[0].set(xlabel="Coverage", ylabel="Bad among retained", title="Development threshold sweep")
    axes[0].legend()
    for split in ("development", "test"):
        subset = [r for r in rows if r["split"] == split and r.get("agreement") is not None]
        for good, style in ((True, "-"), (False, "--")):
            vals = [r["agreement"] for r in subset if r.get("rr_target_ok") is good]
            if vals:
                axes[1].hist(vals, bins=np.linspace(0, 1, 21), histtype="step", linestyle=style,
                             label=f"{split}: {'good' if good else 'bad'}")
    axes[1].set(xlabel="Detector agreement", ylabel="Windows", title="Input SQI vs reference target")
    axes[1].legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(output / "selection.png", dpi=160)
    plt.close(fig)
    for example in examples:
        fs, x = example["fs"], example["signal"]
        fig, ax = plt.subplots(figsize=(12, 3))
        t = np.arange(len(x)) / fs
        ax.plot(t, x, linewidth=.7, color="black")
        for key, color, marker in (("reference", "green", "o"), ("primary", "red", "x"), ("secondary", "blue", "+")):
            peaks = np.asarray(example[key], dtype=int)
            ax.scatter(peaks/fs, x[peaks], s=24, c=color, marker=marker, label=key)
        ax.set(xlabel="Seconds", ylabel="mV", title=example["title"])
        ax.legend(loc="upper right", fontsize=8)
        fig.tight_layout()
        fig.savefig(output / f"example_{example['category']}.png", dpi=160)
        plt.close(fig)
        # Supporting waveform table makes the plotted evidence inspectable.
        peak_sets = {k: set(example[k]) for k in ("reference", "primary", "secondary")}
        write_csv(output / f"example_{example['category']}.csv", [
            {"time_s": float(t[i]), "signal_mv": float(v), **{k: i in s for k, s in peak_sets.items()}}
            for i, v in enumerate(x)])
