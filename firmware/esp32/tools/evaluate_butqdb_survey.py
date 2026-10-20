"""Evaluate every applicable single-lead survey method, with subject grouping."""
from __future__ import annotations
import argparse
import itertools
import json
import pickle
import re
import time
import tracemalloc
import warnings
import numpy as np
import pandas as pd
from sklearn.metrics import confusion_matrix,roc_auc_score
from sklearn.model_selection import StratifiedGroupKFold
from run_butqdb_survey import OUT,FW,read_frame,save_json,sha
from run_cinc2011_verified import factory,score,BASE5

TASKS=['qrs_target','morphology_target']

def method_specs():
    specs=[dict(method='No gate',family='baseline',kind='fixed',columns=[],fixed='all')]
    for name,prefix in [('Orphanidou (EP detector adaptation)','orphanidou'),('Kramer[ECGAssess]','kramer'),('Zhao[NeuroKit]','zhao')]:
        specs.append(dict(method=name,family='fixed',kind='fixed',columns=[prefix+'_score'],fixed=prefix))
    specs.append(dict(method='bSQI F1 >=0.8',family='fixed',kind='fixed',columns=['bsqi_f1'],fixed='bsqi'))
    for t in [.8,.9,1.]:specs.append(dict(method=f'Ho[NeuroKit] >={t:.0%}',family='fixed',kind='fixed',columns=['ho_score'],fixed='ho',threshold=t))
    single={'bSQI':'bsqi_f1',**{k:k for k in ['psqi','ssqi','ksqi','fsqi','bassqi']}}
    for name,col in single.items():
        for kind in ['Logistic','RBF-SVM']:specs.append(dict(method=name+' '+kind,family='single-feature learned',kind=kind,columns=[col]))
    sets={'Project b+p+k+bas':[single[n] for n in ['bSQI','psqi','ksqi','bassqi']],
          'Clifford-5 feature subset':[single[n] for n in ['bSQI','psqi','ksqi','ssqi','fsqi']],
          'Clifford-6 single-lead subset':list(single.values()),'Project-5SQI':BASE5}
    for name,cols in sets.items():
        for kind in (['Logistic'] if name=='Project-5SQI' else ['Logistic','RBF-SVM']):
            specs.append(dict(method=name+' '+kind,family='project feature fusion',kind=kind,columns=cols))
    fixed={'bSQI':'bsqi_f1','Orphanidou':'orphanidou_score','Zhao[NeuroKit]':'zhao_score','Kramer':'kramer_score'}
    for pair in itertools.combinations(fixed,2):
        for kind in ['Logistic','RBF-SVM']:
            specs.append(dict(method=' + '.join(pair)+' -> '+kind,family='project score fusion',kind=kind,columns=[fixed[n] for n in pair]))
    for name,cols in [('bSQI + Logistic-score',['bsqi_f1']),('All-five-score fusion',list(fixed.values()))]:
        specs.append(dict(method=name,family='project stacked fusion',kind='stack',columns=cols))
    specs.append(dict(method='bSQI -> Zhao[NeuroKit] cascade',family='project cascade',kind='cascade',columns=['bsqi_f1','zhao_score']))
    assert len(specs)==42
    return specs

def metrics(y,p,s):
    tn,fp,fn,tp=map(int,confusion_matrix(y,p,labels=[0,1]).ravel())
    se=tp/(tp+fn) if tp+fn else np.nan;sp=tn/(tn+fp) if tn+fp else np.nan
    return dict(n=len(y),tp=tp,fp=fp,tn=tn,fn=fn,accuracy=(tp+tn)/len(y),
                balanced_accuracy=(se+sp)/2,auc=roc_auc_score(y,s) if len(np.unique(y))==2 else np.nan,
                good_recall=se,bad_recall=sp,coverage=(tp+fp)/len(y),
                bad_accepted=fp/(tp+fp) if tp+fp else np.nan,
                bad_pass_rate=fp/(tn+fp) if tn+fp else np.nan)

def cluster_ci(y,p,subjects):
    ids=np.unique(subjects);counts=np.array([confusion_matrix(y[subjects==g],p[subjects==g],labels=[0,1]).ravel() for g in ids])
    rng=np.random.default_rng(42);sample=counts[rng.integers(len(ids),size=(2000,len(ids)))].sum(axis=1)
    tn,fp,fn,tp=sample.T;ok=((tp+fn)>0)&((tn+fp)>0)
    ba=.5*(tp[ok]/(tp[ok]+fn[ok])+tn[ok]/(tn[ok]+fp[ok]))
    return dict(ba_ci_low=float(np.quantile(ba,.025)),ba_ci_high=float(np.quantile(ba,.975)),bootstrap_valid=int(ok.sum()))

def fit_predict(spec,frame,y):
    n=len(frame);pred=np.zeros(n,int);values=np.zeros(n,float);timings=[];sizes=[];errors=[]
    if spec['kind']=='fixed':
        prefix=spec['fixed']
        if prefix=='all':return np.ones(n,int),np.ones(n),dict()
        if prefix=='bsqi':values=frame.bsqi_f1.to_numpy();pred=(values>=.8).astype(int)
        elif prefix=='ho':values=frame.ho_score.to_numpy();pred=(values>=spec['threshold']).astype(int)
        else:values=frame[prefix+'_score'].to_numpy();pred=frame[prefix+'_accept'].to_numpy(int)
        return pred,values,dict()
    for fold in sorted(frame.fold.unique()):
        tr=np.flatnonzero(frame.fold.to_numpy()!=fold);va=np.flatnonzero(frame.fold.to_numpy()==fold)
        assert not set(frame.iloc[tr].subject_id)&set(frame.iloc[va].subject_id)
        cols=spec['columns'];train=frame.iloc[tr][cols].replace([np.inf,-np.inf],np.nan).copy();test=frame.iloc[va][cols].replace([np.inf,-np.inf],np.nan).copy()
        begin=time.perf_counter()
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter('always')
            if spec['kind']=='stack':
                x=frame.iloc[tr][BASE5].replace([np.inf,-np.inf],np.nan);xval=frame.iloc[va][BASE5].replace([np.inf,-np.inf],np.nan)
                inner=np.empty(len(tr))
                groups=frame.iloc[tr].subject_id.to_numpy()
                for it,iv in StratifiedGroupKFold(3,shuffle=True,random_state=17).split(x,y[tr],groups):
                    assert not set(groups[it])&set(groups[iv])
                    assert len(np.unique(y[tr][it]))==2
                    base=factory('Logistic').fit(x.iloc[it],y[tr][it]);inner[iv]=score(base,x.iloc[iv])
                base=factory('Logistic').fit(x,y[tr]);train['logistic_score']=inner;test['logistic_score']=score(base,xval)
            kind=spec['kind'] if spec['kind'] in ['Logistic','RBF-SVM'] else 'Logistic'
            model=factory(kind).fit(train,y[tr]);fit_seconds=time.perf_counter()-begin
            values[va]=score(model,test);pred[va]=model.predict(test)
            if spec['kind']=='cascade':
                b=frame.iloc[va].bsqi_f1.to_numpy();values[va]=np.where(b<=.96,0.,np.where(b>=1.,1.,values[va]));pred[va]=(values[va]>=.5)
            sizes.append(len(pickle.dumps(model,protocol=pickle.HIGHEST_PROTOCOL)))
            if spec['kind']=='stack':sizes[-1]+=len(pickle.dumps(base,protocol=pickle.HIGHEST_PROTOCOL))
            # Trained-stage inference only; upstream feature/gate cost separately measured.
            one=test.iloc[:1];score(model,one);lat=[]
            for _ in range(50):
                start=time.perf_counter_ns();score(model,one);lat.append((time.perf_counter_ns()-start)/1000)
            tracemalloc.start();score(model,test.iloc[:128]);_,peak=tracemalloc.get_traced_memory();tracemalloc.stop()
            timings.append(dict(fold=int(fold),fit_seconds=fit_seconds,p50_us=float(np.median(lat)),p99_us=float(np.percentile(lat,99)),tracemalloc_batch128_bytes=peak))
            errors.extend(str(w.message) for w in caught)
        print('FOLD',spec['method'],fold,'fit_s',round(fit_seconds,2),flush=True)
    return pred,values,dict(model_pickle_max_bytes=max(sizes),model_pickle_mean_bytes=np.mean(sizes),
                            trained_stage_p99_us_max=max(t['p99_us'] for t in timings),timings=timings,warnings=sorted(set(errors)))

def evaluate(task,only=None):
    windows=read_frame(OUT/'windows.csv');features=read_frame(OUT/'features.csv')
    assert len(features)==len(windows) and features.window_id.is_unique
    assert set(features.window_id)==set(windows.window_id)
    frame=windows.merge(features,on='window_id',validate='one_to_one')
    frame['numerical_warning']=(frame.warnings!='[]').astype(int)
    y=frame[task].to_numpy(int);subjects=frame.subject_id.to_numpy()
    folder=OUT/task;folder.mkdir(exist_ok=True)
    for index,spec in enumerate(method_specs()):
        if only and spec['kind'] not in only:continue
        stem=f'{index:02d}';result_path=folder/f'{stem}_metrics.json';prediction_path=folder/f'{stem}_predictions.npz'
        if result_path.exists():
            assert prediction_path.exists();continue
        begin=time.perf_counter();pred,values,cost=fit_predict(spec,frame,y)
        assert np.isfinite(values).all()
        result=dict(task=task,**spec,**metrics(y,pred,values),**cluster_ci(y,pred,subjects),cost=cost,
                    elapsed_seconds=time.perf_counter()-begin)
        per_subject=[];strata=[]
        for g in np.unique(subjects):
            mask=subjects==g;per_subject.append(dict(subject_id=str(g),**metrics(y[mask],pred[mask],values[mask])))
        result['subject_macro_ba_both_classes']=float(np.nanmean([v['balanced_accuracy'] for v in per_subject]))
        result['subjects_with_both_classes']=int(np.sum([np.isfinite(v['balanced_accuracy']) for v in per_subject]))
        for col in ['quality_worst','mixed_classes','numerical_warning']:
            for value in frame[col].unique():
                m=frame[col].to_numpy()==value;strata.append(dict(stratum=col,value=int(value),**metrics(y[m],pred[m],values[m])))
        np.savez_compressed(prediction_path,window_id=frame.window_id.to_numpy(str),target=y,prediction=pred,score=values,
                            fold=frame.fold.to_numpy(),subject=subjects.astype(str))
        save_json(folder/f'{stem}_subjects.json',per_subject);save_json(folder/f'{stem}_strata.json',strata)
        save_json(result_path,result)
        print('RESULT',task,spec['method'],result['balanced_accuracy'],result['auc'],flush=True)

def main():
    p=argparse.ArgumentParser();p.add_argument('--task',choices=TASKS,required=True);p.add_argument('--fixed-only',action='store_true');args=p.parse_args()
    evaluate(args.task,['fixed'] if args.fixed_only else None)

if __name__=='__main__':main()
