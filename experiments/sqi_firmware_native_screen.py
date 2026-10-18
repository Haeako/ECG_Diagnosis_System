"""Screen SQI gates that can be implemented causally with bounded state."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sqi_unified_protocol import evaluate, logistic  # noqa: E402


CANDIDATES = {
    "FW-3": ["normalized_diff_rms", "flatline_ratio", "rr_outlier_fraction"],
    "FW-3-amplitude": ["normalized_diff_rms", "qrs_amplitude_cv",
                       "rr_outlier_fraction"],
    "FW-4-amplitude": ["normalized_diff_rms", "flatline_ratio",
                       "qrs_amplitude_cv", "rr_outlier_fraction"],
    "FW-4-template": ["normalized_diff_rms", "flatline_ratio",
                      "qrs_template_correlation", "rr_outlier_fraction"],
    "FW-5-morphology": ["normalized_diff_rms", "flatline_ratio",
                        "qrs_template_correlation", "qrs_amplitude_cv",
                        "rr_outlier_fraction"],
    "FW-7-counters": ["normalized_diff_rms", "flatline_ratio",
                      "qrs_template_correlation", "qrs_amplitude_cv",
                      "rr_outlier_fraction", "detected_beat_count",
                      "beat_count_disagreement"],
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--windows", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    data = pd.read_csv(args.windows)
    data = data[data.rr_target_ok.notna() & data.agreement.notna()].copy()
    dev = data[data.split == "development"].reset_index(drop=True)
    test = data[data.split == "test"].reset_index(drop=True)
    rows=[]
    for name, features in CANDIDATES.items():
        row, _ = evaluate(name, features, logistic, dev, test, .05, 30)
        row["features"] = ";".join(features)
        rows.append(row)
    out=pd.DataFrame(rows).sort_values("dev_oof_coverage", ascending=False)
    args.output.mkdir(parents=True, exist_ok=True)
    out.to_csv(args.output/"screening.csv", index=False)
    print(out.to_string(index=False))

if __name__ == "__main__": main()
