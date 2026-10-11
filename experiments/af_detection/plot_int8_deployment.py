"""Plot float-versus-int8 metrics from the deployment export report."""

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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--metrics", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = json.loads(args.metrics.read_text(encoding="utf-8"))
    names = ["Balanced accuracy", "Sensitivity", "Specificity", "F1", "ROC-AUC"]
    keys = ["balanced_accuracy", "sensitivity", "specificity", "f1", "roc_auc"]
    floating = [report["float_metrics"][key] for key in keys]
    quantized = [report["int8_metrics"][key] for key in keys]
    x = np.arange(len(names))
    width = 0.36
    fig, axis = plt.subplots(figsize=(9, 5))
    axis.bar(x - width / 2, floating, width, label="Float Extra Trees", color="#277da1")
    axis.bar(x + width / 2, quantized, width, label="INT8 C model", color="#43aa8b")
    axis.set_xticks(x, names, rotation=15, ha="right")
    axis.set_ylim(0.75, 1.0)
    axis.set_ylabel("Held-out record score")
    axis.set_title("Effect of INT8 quantization")
    axis.legend()
    axis.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=180)
    plt.close(fig)


if __name__ == "__main__":
    main()
