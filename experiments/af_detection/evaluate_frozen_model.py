"""Evaluate a frozen AF model on a prepared feature CSV without fitting it."""

from __future__ import annotations

import argparse
import json
import pickle
from pathlib import Path

import pandas as pd
from sklearn.metrics import (
    accuracy_score, balanced_accuracy_score, confusion_matrix, f1_score,
    precision_score, recall_score, roc_auc_score,
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    payload = pickle.loads(args.model.read_bytes())
    model, features = payload["model"], payload["features"]
    frame = pd.read_csv(args.dataset, dtype={"record_id": str})
    missing = {"label", *features}.difference(frame.columns)
    if missing:
        raise SystemExit(f"Dataset is missing columns: {sorted(missing)}")
    prediction = model.predict(frame[features])
    probability = model.predict_proba(frame[features])[:, 1]
    tn, fp, fn, tp = confusion_matrix(frame.label, prediction, labels=[0, 1]).ravel()
    result = {
        "dataset": str(args.dataset), "model": str(args.model),
        "features": features, "windows": len(frame),
        "records": int(frame.record_id.nunique()),
        "accuracy": accuracy_score(frame.label, prediction),
        "balanced_accuracy": balanced_accuracy_score(frame.label, prediction),
        "precision": precision_score(frame.label, prediction, zero_division=0),
        "sensitivity": recall_score(frame.label, prediction, zero_division=0),
        "specificity": tn / (tn + fp) if tn + fp else 0.0,
        "f1": f1_score(frame.label, prediction, zero_division=0),
        "roc_auc": roc_auc_score(frame.label, probability),
        "tn": int(tn), "fp": int(fp), "fn": int(fn), "tp": int(tp),
        "fit_called": False,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
