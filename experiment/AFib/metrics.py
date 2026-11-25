"""AF confusion, ROC AUC and comparable-cohort summaries. Never selects cutoffs."""
import numpy as np
import pandas as pd
from sklearn.metrics import confusion_matrix,roc_auc_score

def score(y,probability,prediction,population=None):
    y=np.asarray(y);s=np.asarray(probability,float);p=np.asarray(prediction,float)
    keep=np.isin(y,[0,1])&np.isfinite(s)&np.isfinite(p)
    y=y[keep];s=s[keep];p=p[keep]
    tn,fp,fn,tp=confusion_matrix(y,p,labels=[0,1]).ravel()
    se=tp/(tp+fn) if tp+fn else np.nan;sp=tn/(tn+fp) if tn+fp else np.nan
    return dict(n=len(y),population=population or len(keep),coverage=len(y)/(population or len(keep)),TP=int(tp),TN=int(tn),FP=int(fp),FN=int(fn),
        sensitivity=se,specificity=sp,ba=(se+sp)/2,f1=2*tp/(2*tp+fp+fn) if 2*tp+fp+fn else np.nan,
        auc=roc_auc_score(y,s) if len(set(y))==2 else np.nan)

def summarize(frame):
    return pd.DataFrame([dict(method=name,**score(g.label,g.score,g.prediction,int(g.population.iloc[0]) if 'population' in g else len(g))) for name,g in frame.groupby('method')]).sort_values('ba',ascending=False)
