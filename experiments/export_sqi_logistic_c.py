"""Train the frozen compact SQI logistic gate and export C golden vectors."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.impute import SimpleImputer
from sklearn.linear_model import LogisticRegression
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler

FEATURES = ["clifford_b_sqi", "spectral_entropy", "normalized_diff_rms",
            "qrs_template_correlation", "rr_outlier_fraction"]


def ff(value: float) -> str:
    text = f"{np.float32(value):.9g}"
    if "." not in text and "e" not in text.lower():
        text += ".0"
    return text + "f"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--windows", type=Path, required=True)
    ap.add_argument("--results", type=Path, required=True)
    ap.add_argument("--firmware", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    data = pd.read_csv(args.windows)
    dev = data[(data.split == "development") & data.rr_target_ok.notna() &
               data.agreement.notna()].reset_index(drop=True)
    test = data[(data.split == "test") & data.rr_target_ok.notna() &
                data.agreement.notna()].reset_index(drop=True)
    model = make_pipeline(SimpleImputer(strategy="median"), StandardScaler(),
                          LogisticRegression(max_iter=2000, class_weight="balanced",
                                             random_state=42))
    model.fit(dev[FEATURES], dev.rr_target_ok.astype(int))
    result = pd.read_csv(args.results)
    threshold = float(result.loc[result.method == "Compact-fusion", "threshold"].iloc[0])
    imp, scaler, lr = (step for _, step in model.steps)
    # Fold scaling into the linear layer: logit = bias + sum(weight*x_imputed).
    weights = lr.coef_[0] / scaler.scale_
    bias = float(lr.intercept_[0] - np.dot(weights, scaler.mean_))

    p = model.predict_proba(test[FEATURES])[:, 1]
    finite = np.isfinite(test[FEATURES].to_numpy()).all(axis=1)
    candidates = np.flatnonzero(finite)
    targets = np.array([0.01, 0.1, 0.3, 0.5, threshold-.01, threshold,
                        threshold+.01, 0.98, 0.995])
    chosen = []
    for target in targets:
        idx = int(candidates[np.argmin(np.abs(p[candidates]-target))])
        if idx not in chosen:
            chosen.append(idx)
    # Add NaN cases to exercise median imputation in C.
    nan_rows = np.flatnonzero(~finite)
    chosen.extend(int(i) for i in nan_rows[:3] if int(i) not in chosen)
    x = test.iloc[chosen][FEATURES].to_numpy(float)
    expected = model.predict_proba(test.iloc[chosen][FEATURES])[:, 1]

    args.firmware.mkdir(parents=True, exist_ok=True)
    header = """#ifndef SQI_GATE_LOGISTIC_H
#define SQI_GATE_LOGISTIC_H
#include <stddef.h>
#define SQI_GATE_FEATURE_COUNT 5U
typedef enum { SQI_GATE_UNUSABLE=0, SQI_GATE_USABLE=1, SQI_GATE_INSUFFICIENT=2 } sqi_gate_decision_t;
float sqi_gate_probability(const float features[SQI_GATE_FEATURE_COUNT]);
sqi_gate_decision_t sqi_gate_predict(const float features[SQI_GATE_FEATURE_COUNT]);
size_t sqi_gate_flash_bytes(void);
size_t sqi_gate_working_ram_bytes(void);
#endif
"""
    source = f'''#include "sqi_gate_logistic.h"
#include <math.h>
static const float median[5] = {{{", ".join(ff(v) for v in imp.statistics_)}}};
static const float weight[5] = {{{", ".join(ff(v) for v in weights)}}};
static const float bias = {ff(bias)};
static const float threshold = {ff(threshold)};
float sqi_gate_probability(const float f[5]) {{
    float z=bias;
    if (!f) return NAN;
    for (unsigned i=0;i<5;i++) {{ float x=isfinite(f[i])?f[i]:median[i]; z += weight[i]*x; }}
    if (z >= 0.0f) {{ float e=expf(-z); return 1.0f/(1.0f+e); }}
    float e=expf(z); return e/(1.0f+e);
}}
sqi_gate_decision_t sqi_gate_predict(const float f[5]) {{
    float p=sqi_gate_probability(f); return isfinite(p) && p>=threshold ? SQI_GATE_USABLE : SQI_GATE_UNUSABLE;
}}
size_t sqi_gate_flash_bytes(void) {{ return sizeof(median)+sizeof(weight)+sizeof(bias)+sizeof(threshold); }}
size_t sqi_gate_working_ram_bytes(void) {{ return sizeof(float)*2U+sizeof(unsigned); }}
'''
    vectors = ["#ifndef SQI_GATE_GOLDEN_VECTORS_H", "#define SQI_GATE_GOLDEN_VECTORS_H",
               "#include <stddef.h>", "typedef struct { float x[5]; float probability; int decision; } sqi_gate_golden_t;",
               f"static const sqi_gate_golden_t SQI_GATE_GOLDEN[{len(chosen)}] = {{"]
    for row, prob in zip(x, expected):
        vals = ["NAN" if not np.isfinite(v) else ff(v) for v in row]
        vectors.append(f"    {{{{{', '.join(vals)}}}, {ff(prob)}, {int(prob >= threshold)}}},")
    vectors += ["};", f"#define SQI_GATE_GOLDEN_COUNT {len(chosen)}U", "#endif"]
    (args.firmware/"sqi_gate_logistic.h").write_text(header, encoding="utf-8")
    (args.firmware/"sqi_gate_logistic.c").write_text(source, encoding="utf-8")
    (args.firmware/"sqi_gate_golden_vectors.h").write_text("\n".join(vectors)+"\n", encoding="utf-8")
    args.output.mkdir(parents=True, exist_ok=True)
    pd.DataFrame(x, columns=FEATURES).assign(expected_probability=expected,
        expected_decision=expected >= threshold).to_csv(args.output/"golden_vectors.csv", index=False)
    meta = {"features": FEATURES, "threshold": threshold, "median": imp.statistics_.tolist(),
            "folded_weights": weights.tolist(), "folded_bias": bias,
            "golden_count": len(chosen), "parameter_flash_bytes": 48,
            "estimated_working_ram_bytes": 12}
    (args.output/"firmware_export.json").write_text(json.dumps(meta, indent=2)+"\n", encoding="utf-8")

if __name__ == "__main__": main()
