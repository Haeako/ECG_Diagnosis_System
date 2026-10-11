"""Quantize a trained Extra Trees model and export deterministic C inference."""

from __future__ import annotations

import argparse
import json
import pickle
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.metrics import accuracy_score, balanced_accuracy_score, confusion_matrix, f1_score, precision_score, recall_score, roc_auc_score


def quant_params(train: pd.DataFrame, features: list[str]) -> tuple[np.ndarray, np.ndarray]:
    low = train[features].quantile(0.001).to_numpy(dtype=np.float64)
    high = train[features].quantile(0.999).to_numpy(dtype=np.float64)
    scales = (high - low) / 255.0
    scales[scales == 0] = 1.0
    zero_points = np.clip(np.rint(-128.0 - low / scales), -128, 127).astype(np.int16)
    return scales, zero_points


def quantize(values: np.ndarray, scales: np.ndarray, zero_points: np.ndarray) -> np.ndarray:
    return np.clip(np.rint(values / scales + zero_points), -128, 127).astype(np.int8)


def export_model(model, features: list[str], scales: np.ndarray, zero_points: np.ndarray):
    nodes = []
    roots = []
    for estimator in model.estimators_:
        tree = estimator.tree_
        root = len(nodes)
        roots.append(root)
        for index in range(tree.node_count):
            left = int(tree.children_left[index])
            right = int(tree.children_right[index])
            if left < 0:
                counts = tree.value[index][0]
                probability = float(counts[1] / counts.sum()) if counts.sum() else 0.0
                nodes.append((-1, -1, -1, 0, int(np.rint(probability * 255.0)), 1))
            else:
                feature = int(tree.feature[index])
                threshold = int(np.floor(tree.threshold[index] / scales[feature] + zero_points[feature]))
                threshold = int(np.clip(threshold, -128, 127))
                nodes.append((root + left, root + right, feature, threshold, 0, 0))
    return nodes, roots


def infer(nodes, roots, values: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    scores = np.empty(len(values), dtype=np.uint16)
    operations = np.empty(len(values), dtype=np.uint16)
    for row_index, row in enumerate(values):
        total = 0
        comparisons = 0
        for root in roots:
            node_index = root
            while not nodes[node_index][5]:
                left, right, feature, threshold, _, _ = nodes[node_index]
                node_index = left if int(row[feature]) <= threshold else right
                comparisons += 1
            total += nodes[node_index][4]
        scores[row_index] = total
        operations[row_index] = comparisons
    return scores, operations


def metrics(truth, prediction, probability) -> dict:
    tn, fp, fn, tp = confusion_matrix(truth, prediction, labels=[0, 1]).ravel()
    return {
        "accuracy": accuracy_score(truth, prediction),
        "balanced_accuracy": balanced_accuracy_score(truth, prediction),
        "precision": precision_score(truth, prediction, zero_division=0),
        "sensitivity": recall_score(truth, prediction, zero_division=0),
        "specificity": tn / (tn + fp), "f1": f1_score(truth, prediction),
        "roc_auc": roc_auc_score(truth, probability),
        "tn": int(tn), "fp": int(fp), "fn": int(fn), "tp": int(tp),
    }


def c_float(value: float) -> str:
    return f"{value:.10g}f"


def write_c(output: Path, features: list[str], scales: np.ndarray, zero_points: np.ndarray,
            nodes: list[tuple], roots: list[int], vectors: np.ndarray, expected_scores: np.ndarray) -> None:
    output.mkdir(parents=True, exist_ok=True)
    header = """#ifndef AF_MODEL_INT8_H\n#define AF_MODEL_INT8_H\n\n#include <stddef.h>\n#include <stdint.h>\n\n#define AF_MODEL_FEATURE_COUNT %dU\n#define AF_MODEL_TREE_COUNT %dU\n#define AF_MODEL_NODE_COUNT %dU\n#define AF_MODEL_SCORE_THRESHOLD %dU\n\ntypedef struct {\n    uint16_t score;\n    uint16_t comparisons;\n} af_model_result_t;\n\nvoid af_model_quantize(const float features[AF_MODEL_FEATURE_COUNT], int8_t output[AF_MODEL_FEATURE_COUNT]);\naf_model_result_t af_model_predict_int8(const int8_t features[AF_MODEL_FEATURE_COUNT]);\nint af_model_predict(const float features[AF_MODEL_FEATURE_COUNT]);\nsize_t af_model_flash_bytes(void);\nsize_t af_model_working_ram_bytes(void);\n\n#endif\n""" % (len(features), len(roots), len(nodes), len(roots) * 128)
    (output / "af_model_int8.h").write_text(header, encoding="ascii")

    lines = [
        '#include "af_model_int8.h"', "", "#include <math.h>", "",
        "typedef struct {", "    int16_t left;", "    int16_t right;", "    int8_t feature;",
        "    int8_t threshold;", "    uint8_t probability;", "    uint8_t is_leaf;", "} af_node_t;", "",
        "static const float feature_scales[AF_MODEL_FEATURE_COUNT] = {" + ", ".join(c_float(x) for x in scales) + "};",
        "static const int8_t feature_zero_points[AF_MODEL_FEATURE_COUNT] = {" + ", ".join(str(int(x)) for x in zero_points) + "};",
        "static const uint16_t tree_roots[AF_MODEL_TREE_COUNT] = {" + ", ".join(str(x) for x in roots) + "};",
        "static const af_node_t nodes[AF_MODEL_NODE_COUNT] = {",
    ]
    lines.extend(f"    {{{left}, {right}, {feature}, {threshold}, {probability}, {leaf}}}," for left, right, feature, threshold, probability, leaf in nodes)
    lines.extend([
        "};", "",
        "void af_model_quantize(const float features[AF_MODEL_FEATURE_COUNT], int8_t output[AF_MODEL_FEATURE_COUNT])",
        "{", "    for (size_t i = 0; i < AF_MODEL_FEATURE_COUNT; ++i) {",
        "        long value = lroundf(features[i] / feature_scales[i]) + feature_zero_points[i];",
        "        if (value < -128) value = -128;", "        if (value > 127) value = 127;",
        "        output[i] = (int8_t)value;", "    }", "}", "",
        "af_model_result_t af_model_predict_int8(const int8_t features[AF_MODEL_FEATURE_COUNT])",
        "{", "    uint32_t score = 0;", "    uint16_t comparisons = 0;",
        "    for (size_t tree = 0; tree < AF_MODEL_TREE_COUNT; ++tree) {", "        int16_t index = (int16_t)tree_roots[tree];",
        "        while (!nodes[index].is_leaf) {", "            const af_node_t *node = &nodes[index];",
        "            index = features[node->feature] <= node->threshold ? node->left : node->right;", "            ++comparisons;", "        }",
        "        score += nodes[index].probability;", "    }",
        "    af_model_result_t result = {(uint16_t)score, comparisons};", "    return result;", "}", "",
        "int af_model_predict(const float features[AF_MODEL_FEATURE_COUNT])", "{", "    int8_t quantized[AF_MODEL_FEATURE_COUNT];",
        "    af_model_quantize(features, quantized);", "    return af_model_predict_int8(quantized).score >= AF_MODEL_SCORE_THRESHOLD;", "}", "",
        "size_t af_model_flash_bytes(void)", "{", "    return sizeof(nodes) + sizeof(tree_roots) + sizeof(feature_scales) + sizeof(feature_zero_points);", "}",
        "size_t af_model_working_ram_bytes(void)", "{", "    return AF_MODEL_FEATURE_COUNT * sizeof(int8_t) + sizeof(af_model_result_t) + sizeof(uint32_t);", "}",
    ])
    (output / "af_model_int8.c").write_text("\n".join(lines) + "\n", encoding="ascii")

    vector_lines = ["#ifndef AF_MODEL_TEST_VECTORS_H", "#define AF_MODEL_TEST_VECTORS_H", "", f"#define AF_MODEL_TEST_VECTOR_COUNT {len(vectors)}U",
                    "static const int8_t af_test_vectors[AF_MODEL_TEST_VECTOR_COUNT][AF_MODEL_FEATURE_COUNT] = {"]
    vector_lines.extend("    {" + ", ".join(str(int(x)) for x in row) + "}," for row in vectors)
    vector_lines.extend(["};", "static const uint16_t af_test_scores[AF_MODEL_TEST_VECTOR_COUNT] = {" + ", ".join(str(int(x)) for x in expected_scores) + "};", "", "#endif", ""])
    (output / "af_model_test_vectors.h").write_text("\n".join(vector_lines), encoding="ascii")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--experiment", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--metrics", type=Path, required=True)
    args = parser.parse_args()
    payload = pickle.loads(args.model.read_bytes())
    model, features = payload["model"], payload["features"]
    experiment = json.loads(args.experiment.read_text(encoding="utf-8"))
    frame = pd.read_csv(args.dataset, dtype={"record_id": str})
    train = frame[frame.record_id.isin(experiment["train_records"])]
    test = frame[frame.record_id.isin(experiment["test_records"])]
    scales, zero_points = quant_params(train, features)
    nodes, roots = export_model(model, features, scales, zero_points)
    quantized = quantize(test[features].to_numpy(), scales, zero_points)
    scores, operations = infer(nodes, roots, quantized)
    probability = scores.astype(np.float64) / (255.0 * len(roots))
    prediction = scores >= len(roots) * 128
    float_probability = model.predict_proba(test[features])[:, 1]
    float_prediction = model.predict(test[features])
    report = {
        "features": features, "trees": len(roots), "nodes": len(nodes),
        "max_depth": int(max(x.tree_.max_depth for x in model.estimators_)),
        "quantization": "asymmetric int8 features/thresholds; uint8 leaf probability",
        "calibration": "0.1th to 99.9th percentiles of train records only",
        "float_metrics": metrics(test.label, float_prediction, float_probability),
        "int8_metrics": metrics(test.label, prediction, probability),
        "prediction_agreement": float(np.mean(prediction == float_prediction)),
        "mean_comparisons": float(np.mean(operations)), "max_comparisons": int(np.max(operations)),
        "estimated_model_bytes": len(nodes) * 8 + len(roots) * 2 + len(features) * 5,
        "estimated_working_ram_bytes": len(features) + 8,
        "scales": dict(zip(features, scales.tolist())), "zero_points": dict(zip(features, zero_points.astype(int).tolist())),
    }
    args.metrics.parent.mkdir(parents=True, exist_ok=True)
    args.metrics.write_text(json.dumps(report, indent=2), encoding="utf-8")
    vector_count = min(512, len(test))
    indices = np.linspace(0, len(test) - 1, vector_count, dtype=int)
    write_c(args.output_dir, features, scales, zero_points, nodes, roots, quantized[indices], scores[indices])
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
