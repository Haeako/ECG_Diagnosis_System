"""Frozen random_forest_rr; only its own trusted local artifact is loaded."""
METHOD='random_forest_rr'
ARTIFACT='random_forest_selected.pkl'
FEATURES=('pnn50','shannon_entropy','sd2_ms','sd1_sd2','median_rr_ms')
def load(output=None):
    import pickle
    from AFib.utils._shared import ROOT
    payload=pickle.loads((ROOT/'report/outputs/af_detection/final_benchmark/models'/ARTIFACT).read_bytes())
    assert tuple(payload['features'])==FEATURES
    return payload['model']
def predict(model,frame):
    score=model.predict_proba(frame.loc[:,FEATURES])[:,1]
    return score,(score>=.5).astype(int)
