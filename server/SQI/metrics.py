"""Quality-score AUC and explicit valid/common coverage, never threshold fitting."""
import numpy as np
import pandas as pd
from sklearn.metrics import roc_auc_score

def summarize(predictions,labels):
    if predictions.duplicated(['window_id','method']).any():raise ValueError('Duplicate SQI prediction')
    scores=predictions.pivot(index='window_id',columns='method',values='score').reindex(labels.window_id)
    y=labels.set_index('window_id').reindex(scores.index).qrs_target.astype(int)
    common=np.isfinite(scores).all(axis=1);rows=[]
    def auc(target,score):return roc_auc_score(target,score) if len(set(target))==2 else np.nan
    for method in scores:
        valid=np.isfinite(scores[method]);rows.append(dict(method=method,windows=len(y),valid=int(valid.sum()),coverage=float(valid.mean()),
            common_valid=int(common.sum()),auc_common=auc(y[common],scores.loc[common,method]),auc_valid=auc(y[valid],scores.loc[valid,method])))
    return pd.DataFrame(rows).sort_values('auc_common',ascending=False)
