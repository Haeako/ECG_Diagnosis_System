"""ECGFounder + Extra Trees INT8 RR + 13-feature Logistic Regression fusion."""
import numpy as np
from typing import Any
from AFib.metrics import score as _metrics
import pandas as pd
from sklearn.impute import SimpleImputer
from sklearn.linear_model import LogisticRegression
from sklearn.preprocessing import StandardScaler
from sklearn.pipeline import Pipeline
from sklearn.metrics import roc_auc_score
from sklearn.model_selection import StratifiedGroupKFold
from AFib.utils.reproducibility import SEED
METHOD='fusion_lr_full_shdb'
FEATURES = (
    "p_device", "p_server", "device_sqi_score", "device_sqi_usable",
    "device_sqi_bad", "device_age_s", "server_template_correlation",
    "server_rr_cv", "server_n_peaks", "bpm_device", "bpm_server",
    "bpm_abs_difference", "score_disagreement",
)

def _flags(frame: pd.DataFrame, name: str) -> np.ndarray:
    values = frame[name]
    if not values.dropna().isin([True, False, 0, 1]).all():
        raise ValueError(f"{name} must contain booleans or 0/1, not text")
    return values.fillna(False).to_numpy(dtype=bool)

def _new_pipeline() -> Pipeline:
    return Pipeline([
        ("imputer", SimpleImputer(strategy="median", add_indicator=True, keep_empty_features=True)),
        ("scaler", StandardScaler()),
        ("classifier", LogisticRegression(
            C=1.0, class_weight="balanced", solver="liblinear",
            max_iter=2000, random_state=SEED,
        )),
    ])


def _choose_threshold(y: np.ndarray, score: np.ndarray) -> tuple[float, dict[str, Any]]:
    """Validation-only BA optimum; ties closest to .5, then smaller cutoff."""
    y, score = np.asarray(y, dtype=int), np.asarray(score, dtype=float)
    if set(np.unique(y)) != {0, 1} or not np.isfinite(score).all():
        raise ValueError("Threshold selection needs two validation classes and finite scores")
    candidates = np.unique(np.r_[0.0, 0.5, 1.0, score, np.nextafter(score.max(), np.inf)])
    best: tuple[float, float, float] | None = None
    chosen = 0.5
    for cutoff in candidates:
        pred = score >= cutoff
        sensitivity = float(pred[y == 1].mean())
        specificity = float((~pred[y == 0]).mean())
        quality = round((sensitivity + specificity) / 2, 14)
        key = (quality, -abs(float(cutoff) - 0.5), -float(cutoff))
        if best is None or key > best:
            best, chosen = key, float(cutoff)
    return chosen, _metrics(y, score, score >= chosen)

def predict_with_fallback(frame: pd.DataFrame, pipeline: Pipeline,
                          thresholds: dict[str, float]) -> pd.DataFrame:
    """Apply a fitted paired-input LR; route absent branches explicitly.

    A cached, noncurrent server result never substitutes for the current
    server. Fallback probabilities use their branch's validation cutoff.
    Labels, identities and timestamps never enter the fitted pipeline.
    """
    values = frame.loc[:, FEATURES].apply(pd.to_numeric, errors="raise").astype(float)
    values = values.replace([np.inf, -np.inf], np.nan)
    p_device, p_server = values.p_device.to_numpy(), values.p_server.to_numpy()
    device_ok = _flags(frame, "device_valid") & np.isfinite(p_device)
    server_ok = (_flags(frame, "server_current") & _flags(frame, "server_valid")
                 & np.isfinite(p_server))
    paired = device_ok & server_ok
    result = pd.DataFrame(index=frame.index)
    result["paired_input_valid"] = paired
    result["p_fusion_lr"] = np.nan
    if paired.any():
        result.loc[paired, "p_fusion_lr"] = pipeline.predict_proba(values.loc[paired])[:, 1]
    result["p_fusion_fallback"] = np.nan
    result["fallback_threshold"] = np.nan
    result["fallback_route"] = "unavailable"
    for mask, probabilities, threshold, route in (
        (paired, result.p_fusion_lr.to_numpy(), thresholds["fusion_lr"], "fusion_lr"),
        (~paired & server_ok, p_server, thresholds["server"], "server_only"),
        (~paired & ~server_ok & device_ok, p_device, thresholds["device"], "device_only"),
    ):
        result.loc[mask, "p_fusion_fallback"] = probabilities[mask]
        result.loc[mask, "fallback_threshold"] = threshold
        result.loc[mask, "fallback_route"] = route
    result["fusion_fallback_prediction"] = np.nan
    usable = result.p_fusion_fallback.notna()
    result.loc[usable, "fusion_fallback_prediction"] = (
        result.loc[usable, "p_fusion_fallback"] >= result.loc[usable, "fallback_threshold"]
    ).astype(int)
    return result
def load(output=None):
    import joblib,json
    from AFib.utils._shared import ROOT,checked
    folder=ROOT/'outputs/AFib/fusion_lr_full_shdb_20261118'
    audit=json.loads((folder/'publication_audit.json').read_text())
    model=joblib.load(checked(folder/'fusion_lr_full_shdb.joblib',audit['model_sha256']))
    threshold=json.loads((folder/'protocol.json').read_text())['threshold']
    return model,threshold

def predict(loaded,frame):
    from AFib.utils._shared import ROOT
    import json
    model,threshold=loaded
    base=json.loads((ROOT/'report/benchmark_runs/af_fusion_published_swt_20261103/models/metadata.json').read_text())
    r=predict_with_fallback(frame,model,dict(base['thresholds'],fusion_lr=threshold))
    return r.p_fusion_fallback.to_numpy(),r.fusion_fallback_prediction.to_numpy()

def train(frame,output):
    """All prepared SHDB subjects; threshold from group-disjoint OOF only."""
    from sklearn.metrics import roc_auc_score
    import joblib
    from AFib.utils._shared import save
    if not frame.dataset.eq('SHDB-AF').all():raise ValueError('Fusion training accepts SHDB only')
    keep=frame.device_valid & frame.server_valid & frame.server_current & frame.y.isin([0,1])
    f=frame[keep].reset_index(drop=True);x=f.loc[:,FEATURES].replace([np.inf,-np.inf],np.nan).astype(float);y=f.y.astype(int)
    oof=np.full(len(f),np.nan)
    for tr,va in StratifiedGroupKFold(5,shuffle=True,random_state=SEED).split(x,y,f.group_id):
        assert not set(f.group_id.iloc[tr]) & set(f.group_id.iloc[va])
        m=_new_pipeline().fit(x.iloc[tr],y.iloc[tr]);oof[va]=m.predict_proba(x.iloc[va])[:,1]
    threshold,_=_choose_threshold(y,oof)
    model=_new_pipeline().fit(x,y);output.mkdir(parents=True,exist_ok=True)
    joblib.dump(model,output/'fusion_lr.joblib')
    save(output/'training.json',dict(groups=f.group_id.nunique(),windows=len(f),threshold=threshold,threshold_selection='5fold subject-group OOF SHDB only'))
    return model,threshold
