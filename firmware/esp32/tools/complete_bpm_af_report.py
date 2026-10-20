"""Rebuild BPM/RR and frozen AF evaluation; intentionally no SQI evaluation.

All writes stay in firmware/esp32/benchmark_runs/non_sqi_20261019.
Never retrains or selects thresholds using held-out records.
"""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
import os
import pickle
import platform
import subprocess
import sys
import time
import tracemalloc
from pathlib import Path

import numpy as np
import pandas as pd
import scipy.signal as sig
import wfdb
from sklearn.metrics import confusion_matrix, roc_auc_score

FW = Path(__file__).resolve().parents[1]
ROOT = FW.parents[1]
OLD = ROOT / 'outputs'
ARCHIVE = Path(r'D:\DOC\ECG_PAPER\temp\benchmark_results')
OUT = FW / 'benchmark_runs/non_sqi_20261019'
sys.path.insert(0, str(ROOT / 'experiments/af_detection'))
from features import rr_features, FEATURE_NAMES

SOURCES = [FW/'tools/non_sqi_replay_host.c',
           FW/'main/task/Core1/ECGPipeline/ExFeature/ExtractFeature.c',
           FW/'main/components/filter/SBfilter.c',
           FW/'main/components/af_model/af_runtime.c',
           FW/'main/components/af_model/af_model_int8.c']
BEATS = set('NLRBAaJSVrFejnE/Q?')

def read_json(p):
    return json.loads(Path(p).read_text(encoding='utf-8-sig'))

def write_json(p, x):
    Path(p).write_text(json.dumps(x, indent=2, ensure_ascii=False, default=str), encoding='utf-8')

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def load_csv(p):
    return pd.read_csv(p, dtype={'record_id': str, 'record': str}, float_precision='round_trip')

def compile_host():
    dll = OUT/'current_runtime.dll'
    subprocess.run(['gcc','-std=c99','-O2','-shared',f'-I{FW / "main"}',
                    *map(str,SOURCES),'-o',str(dll)],check=True)
    lib = ctypes.CDLL(str(dll))
    lib.replay.argtypes = [np.ctypeslib.ndpointer(np.int32,flags='C_CONTIGUOUS'),ctypes.c_uint,
                          *[np.ctypeslib.ndpointer(np.uint32,flags='C_CONTIGUOUS')]*4,ctypes.c_int]
    lib.replay.restype = ctypes.c_uint
    lib.predict_many.argtypes = [np.ctypeslib.ndpointer(np.float32,flags='C_CONTIGUOUS'),ctypes.c_uint,
                                np.ctypeslib.ndpointer(np.uint16,flags='C_CONTIGUOUS')]
    return lib

def adc_from_mv(x, fs):
    assert np.isfinite(x).all()
    x = sig.resample_poly(x, 360, int(fs))
    hp = sig.butter(1,.5,btype='highpass',fs=360,output='sos')
    x,_ = sig.sosfilt(hp,x,zi=sig.sosfilt_zi(hp)*x[0])
    x = np.rint(x*200+2048)
    clipped = int(np.sum((x<0)|(x>4095)))
    return np.ascontiguousarray(np.clip(x,0,4095),dtype=np.int32),clipped

def replay(lib, adc, af):
    # Refractory period is 300ms; n/50 leaves ample output capacity.
    capacity = len(adc)//50+100
    p=np.empty(capacity,np.uint32)
    b=np.empty((len(adc)//1800+10,2),np.uint32)
    e=np.empty((capacity,8),np.uint32)
    counts=np.zeros(3,np.uint32)
    before=time.perf_counter()
    lib.replay(adc,len(adc),p,b,e,counts,int(af))
    elapsed=time.perf_counter()-before
    return p[:counts[0]].copy(), b[:counts[1]].copy(), e[:counts[2]].copy(), elapsed

def match(ref, est, tol=50):
    pairs=[]; i=j=0
    while i<len(ref) and j<len(est):
        if est[j]<ref[i]-tol: j+=1
        elif est[j]>ref[i]+tol: i+=1
        else:
            pairs.append((i,j)); i+=1; j+=1
    return np.asarray(pairs,dtype=int).reshape(-1,2)

def error_metrics(errors):
    a=np.asarray(errors,float); good=np.isfinite(a); x=a[good]
    return dict(total=len(a), valid=int(good.sum()),coverage=float(good.mean()) if len(a) else 0,
                mae=float(np.abs(x).mean()) if len(x) else None,
                rmse=float(np.sqrt(np.mean(x*x))) if len(x) else None,
                p95_abs=float(np.percentile(np.abs(x),95)) if len(x) else None,
                within_5_all=float(np.sum(np.abs(x)<=5)/len(a)) if len(a) else 0)

def rhythm_changes(ann, fs):
    changes=[(s*1000.0/fs,a.strip()[1:].strip().upper()) for s,a in zip(ann.sample,ann.aux_note)
             if a.strip().startswith('(')]
    return np.asarray([a for a,b in changes]), np.asarray([b for a,b in changes])

def pure_label(start, end, changes):
    t, states=changes
    k=np.searchsorted(t,start,side='right')-1
    if k<0: return -1
    last=np.searchsorted(t,end,side='right')
    labels=states[k:last]
    if not len(labels) or np.any(labels=='UNKNOWN'): return -1
    af=np.isin(labels,['AF','AFIB'])
    return 1 if af.all() else (0 if not af.any() else -1)

def af_features(peaks,changes,record):
    rows=[]; candidate=invalid=mixed=0
    for i in range(0,len(peaks)-30,15):
        candidate+=1
        win=peaks[i:i+31].astype(np.float64)
        rr=np.diff(win)
        if np.any((rr<250)|(rr>2500)): invalid+=1; continue
        label=pure_label(win[0],win[-1],changes)
        if label<0: mixed+=1; continue
        rows.append(dict(record_id=record,window_start_ms=win[0],window_end_ms=win[-1],
                         label=label,**rr_features(rr)))
    return rows,dict(candidate=candidate,invalid_rr=invalid,mixed_unknown=mixed,valid=len(rows))

def add_noise(raw,fs,noise,nfs,snr,index):
    if snr is None: return raw.copy()
    n=sig.resample_poly(noise,fs,nfs)
    n=np.resize(np.roll(n,-((index*104729)%len(n))),len(raw))
    result=raw.copy()
    for start in range(10*fs,len(raw),30*fs):
        stop=min(len(raw),start+10*fs)
        x=raw[start:stop]-raw[start:stop].mean()
        y=n[start:stop]-n[start:stop].mean()
        xr=np.sqrt(np.mean(x*x)); yr=np.sqrt(np.mean(y*y))
        if xr>0 and yr>0: result[start:stop]+=y*(xr/yr/10**(snr/20))
    return result

def run_replay(lib):
    summaries=[]; bpms=[]; rr_rows=[]; peak_rows=[]; durations=[]
    selection=read_json(OLD/'firmware_qtdb_mlii_verified/selection.json')['included']
    for selected in selection:
        name=selected['record']; path=ROOT/'data/qtdb'/name
        r=wfdb.rdrecord(str(path)); ann=wfdb.rdann(str(path),'atr')
        ref=np.asarray([s*1000/r.fs for s,sym in zip(ann.sample,ann.symbol) if sym in BEATS])
        adc,clipped=adc_from_mv(r.p_signal[:,selected['channel']],r.fs)
        peaks,bpm,events,sec=replay(lib,adc,False)
        pairs=match(ref,peaks.astype(float)); tp=len(pairs)
        summaries.append(dict(record=name,TP=tp,FP=len(peaks)-tp,FN=len(ref)-tp,clipped=clipped,
                              samples=len(adc),seconds=len(adc)/360,host_replay_seconds=sec))
        for ri,ei in pairs:
            peak_rows.append(dict(record=name,reference_ms=ref[ri],estimate_ms=int(peaks[ei]),
                                  error_ms=float(peaks[ei])-ref[ri]))
        # Adjacent both in reference AND prediction: no gaps disguised as one RR.
        for (ri,ei),(rj,ej) in zip(pairs[:-1],pairs[1:]):
            if rj==ri+1 and ej==ei+1:
                true_rr=ref[rj]-ref[ri]; pred_rr=float(peaks[ej])-float(peaks[ei])
                rr_rows.append(dict(record=name,reference_rr_ms=true_rr,estimate_rr_ms=pred_rr,
                                    error_ms=pred_rr-true_rr,bpm_error=60000/pred_rr-60000/true_rr))
        # Score the ACTUAL 5-second firmware BPM field, not a replacement estimator.
        for end,value in bpm:
            end=float(end)
            idx=np.where((ref[1:]>end-5000)&(ref[1:]<=end))[0]+1
            expected=60000/np.mean(ref[idx]-ref[idx-1]) if len(idx) else np.nan
            error=float(value)-expected if value>0 else np.nan
            bpms.append(dict(record=name,end_ms=end,reference_bpm=expected,firmware_bpm=int(value),error=error))
        np.savez_compressed(OUT/f'qtdb_{name}_peaks.npz',reference_ms=ref,estimate_ms=peaks)
        print('QTDB',name,'TP/FP/FN',tp,len(peaks)-tp,len(ref)-tp,flush=True)
    pd.DataFrame(summaries).to_csv(OUT/'bpm_qrs_per_record.csv',index=False)
    pd.DataFrame(bpms).to_csv(OUT/'bpm_display_windows.csv',index=False)
    pd.DataFrame(rr_rows).to_csv(OUT/'rr_adjacent_pairs.csv',index=False)
    pd.DataFrame(peak_rows).to_csv(OUT/'qrs_matched_peaks.csv',index=False)
    tp=sum(x['TP'] for x in summaries); fp=sum(x['FP'] for x in summaries); fn=sum(x['FN'] for x in summaries)
    write_json(OUT/'bpm_current_metrics.json',dict(records=len(summaries),TP=tp,FP=fp,FN=fn,
        sensitivity=tp/(tp+fn),ppv=tp/(tp+fp),f1=2*tp/(2*tp+fp+fn),event_accuracy=tp/(tp+fp+fn),
        display_bpm=error_metrics([x['error'] for x in bpms]),
        adjacent_rr=error_metrics([x['error_ms'] for x in rr_rows]),
        adjacent_rr_reference_coverage=len(rr_rows)/(tp+fn-len(summaries)),
        instantaneous_bpm_matched_only=error_metrics([x['bpm_error'] for x in rr_rows]),
        display_reference='60000 / mean reference RR whose right endpoint falls in preceding 5s; first boundary 5s; zeros are missing',
        rr_protocol='one-to-one +/-50ms; adjacent indices in BOTH series only; conditional error plus reference-pair coverage',
        preprocessing='250->360Hz polyphase; causal 0.5Hz first-order HP mimics AFE; 200counts/mV+2048 clipped12bit; production notch/QRS',
        source_sha256={str(p):sha(p) for p in SOURCES}))

    exp=read_json(OLD/'af_detection/final_benchmark/experiment.json')
    names=[n for n in exp['test_records'] if (ROOT/f'data/afdb/{n}.dat').exists()]
    noise,meta=wfdb.rdsamp(str(ROOT/'data/nstdb/ma'),channels=[0])
    data_hashes={}; cohort=[]
    for condition,snr in [('clean',None),('ma_12dB',12),('ma_6dB',6),('ma_0dB',0)]:
        allrows=[]; runtime=[]
        for index,name in enumerate(names):
            path=ROOT/'data/afdb'/name
            r=wfdb.rdrecord(str(path),channels=[0])
            changes=rhythm_changes(wfdb.rdann(str(path),'atr'),r.fs)
            raw=add_noise(r.p_signal[:,0],int(r.fs),noise[:,0],int(meta['fs']),snr,index)
            adc,clipped=adc_from_mv(raw,r.fs)
            peaks,bpm,events,sec=replay(lib,adc,True)
            # Convert to float BEFORE fs multiplication; timestamps must be monotonic.
            assert np.all(np.diff(peaks.astype(np.int64))>0)
            samples=np.rint(peaks.astype(float)*r.fs/1000).astype(np.int64)
            assert np.all(np.diff(samples)>0) and (not len(samples) or samples[-1]<r.sig_len)
            rows,counts=af_features(peaks,changes,name)
            allrows.extend(rows)
            for e in events:
                label=pure_label(float(e[1]),float(e[0]),changes)
                if label>=0:
                    runtime.append(dict(record_id=name,start_ms=int(e[1]),end_ms=int(e[0]),label=label,
                                        score=int(e[2]),probability=e[3]/10000,raw=int(e[4]),confirmed=int(e[5])))
            cohort.append(dict(condition=condition,record=name,source_samples=r.sig_len,fs=r.fs,
                               source_lead=r.sig_name[0],detected_peaks=len(peaks),clipped_samples=clipped,
                               runtime_emitted=len(events),host_replay_seconds=sec,**counts))
            np.savez_compressed(OUT/f'afdb_{condition}_{name}_peaks.npz',estimate_ms=peaks)
            if condition=='clean':
                for suffix in ['.dat','.hea','.atr']:
                    data_hashes[str(path)+suffix]=sha(str(path)+suffix)
            print('AFDB',condition,name,'valid/candidate',len(rows),counts['candidate'],'host_s',round(sec,2),flush=True)
        pd.DataFrame(allrows).to_csv(OUT/f'af_features_{condition}.csv',index=False)
        pd.DataFrame(runtime).to_csv(OUT/f'af_runtime_{condition}.csv',index=False)
        pd.DataFrame(cohort).to_csv(OUT/'af_cohort.csv',index=False)
    for suffix in ['.dat','.hea']: data_hashes[str(ROOT/'data/nstdb/ma')+suffix]=sha(str(ROOT/'data/nstdb/ma')+suffix)
    write_json(OUT/'replay_manifest.json',dict(test_records=names,
        excluded=[dict(record=n,reason='no waveform') for n in exp['test_records'] if n not in names],
        source_sha256={str(p):sha(p) for p in SOURCES},data_sha256=data_hashes,
        no_sqi_calls=True,af_features='30 RR, stride15; float timestamp conversion before fs multiplication; no resnapping RR to source grid',
        labels='atr continuous rhythm over entire window; pure AF vs pure known nonAF; reject transitions/unknown',
        model_training='Frozen existing models; zero refits, zero test-set tuning',
        noise='NSTDB ma ch0:10s burst every30s starting10s; local centered RMS SNR12/6/0dB; offset=index*104729'))

def classification(y,p,s):
    y=np.asarray(y,int); p=np.asarray(p,int)
    tn,fp,fn,tp=map(int,confusion_matrix(y,p,labels=[0,1]).ravel())
    se=tp/(tp+fn) if tp+fn else np.nan; sp=tn/(tn+fp) if tn+fp else np.nan
    return dict(windows=len(y),accuracy=(tn+tp)/len(y),balanced_accuracy=(se+sp)/2,
                sensitivity=se,specificity=sp,precision=tp/(tp+fp) if tp+fp else 0,
                f1=2*tp/(2*tp+fp+fn) if 2*tp+fp+fn else 0,
                roc_auc=roc_auc_score(y,s) if len(np.unique(y))==2 else np.nan,tn=tn,fp=fp,fn=fn,tp=tp)

def evaluate_models(lib):
    exp=read_json(OLD/'af_detection/final_benchmark/experiment.json')
    assert not set(exp['train_records'])&set(exp['test_records'])
    annotations=load_csv(OLD/'af_detection/features.csv')
    test=annotations[annotations.record_id.isin(exp['test_records'])].copy()
    assert len(test)==exp['test_windows']
    cohort=load_csv(OUT/'af_cohort.csv')
    names=sorted(cohort.record.unique())
    datasets={'annotation_test7':test,'annotation_matched6':test[test.record_id.isin(names)].copy()}
    for condition in cohort.condition.unique():
        datasets[f'current_{condition}']=load_csv(OUT/f'af_features_{condition}.csv')
    models=[]; costs=[]; rows=[]; predictions=[]; audits=[]
    dataset_rows=[]
    for dataset, frame in {'annotation_train18':annotations[annotations.record_id.isin(exp['train_records'])],**datasets}.items():
        dataset_rows.append(dict(dataset=dataset,records=frame.record_id.nunique(),windows=len(frame),
                                 af=int((frame.label==1).sum()),non_af=int((frame.label==0).sum())))
    pd.DataFrame(dataset_rows).to_csv(OUT/'dataset_counts.csv',index=False)
    oldmetrics=load_csv(OLD/'af_detection/final_benchmark/metrics.csv')
    for path in sorted((OLD/'af_detection/final_benchmark/models').glob('*.pkl')):
        payload=pickle.loads(path.read_bytes()); model=payload['model']; features=payload['features']
        models.append((path.stem,model,features))
        X=test[features].iloc[:256]
        model.predict_proba(X.iloc[:1])
        latency=[]
        for i in range(200):
            x=X.iloc[[i%len(X)]]
            before=time.perf_counter_ns(); model.predict_proba(x); latency.append((time.perf_counter_ns()-before)/1000)
        tracemalloc.start(); model.predict_proba(X); _,peak=tracemalloc.get_traced_memory(); tracemalloc.stop()
        costs.append(dict(model=path.stem,features=len(features),pickle_bytes=path.stat().st_size,
                          host_predict_p50_us=float(np.percentile(latency,50)),host_predict_p99_us=float(np.percentile(latency,99)),
                          timed_calls=200,python_tracemalloc_peak_bytes_batch256=peak,sha256=sha(path)))
        for dataset,frame in datasets.items():
            assert set(frame.record_id)<=set(exp['test_records'])
            assert np.isfinite(frame[features].to_numpy()).all()
            prob=model.predict_proba(frame[features])[:,1]; pred=model.predict(frame[features])
            result=classification(frame.label,pred,prob)
            rows.append(dict(dataset=dataset,model=path.stem,records=frame.record_id.nunique(),**result))
            p=frame[['record_id','label']].copy(); p['row_id']=np.arange(len(frame))
            p['dataset']=dataset;p['model']=path.stem;p['prediction']=pred;p['probability']=prob
            predictions.append(p)
            if dataset=='annotation_test7':
                model_name=path.stem.rsplit('_',1)[0].replace('_',' ')
                prev=oldmetrics[(oldmetrics.model.str.lower()==model_name)&(oldmetrics.feature_set==path.stem.rsplit('_',1)[1])].iloc[0]
                delta=max(abs(result[k]-prev[k]) for k in ['accuracy','balanced_accuracy','roc_auc','tn','fp','fn','tp'])
                audits.append(dict(model=path.stem,max_original_metric_delta=delta))
        print('AF frozen evaluated',path.stem,flush=True)
    # Actual exported INT8 C inference on all six datasets, not a size estimate.
    selected=exp['selected_features']
    for dataset,frame in datasets.items():
        x=np.ascontiguousarray(frame[selected],dtype=np.float32); scores=np.empty(len(x),np.uint16)
        lib.predict_many(x,len(x),scores); probs=scores.astype(float)/(80*255)
        pred=(scores>=10240).astype(int)
        rows.append(dict(dataset=dataset,model='extra_trees_selected_INT8_C',records=frame.record_id.nunique(),
                         **classification(frame.label,pred,probs)))
        p=frame[['record_id','label']].copy();p['row_id']=np.arange(len(frame));p['dataset']=dataset
        p['model']='extra_trees_selected_INT8_C';p['prediction']=pred;p['probability']=probs;predictions.append(p)
    latency=[]
    x=np.ascontiguousarray(test[selected].iloc[:200],dtype=np.float32)
    scores=np.empty(1,np.uint16)
    for i in range(len(x)):
        one=x[i:i+1]
        before=time.perf_counter_ns();lib.predict_many(one,1,scores)
        latency.append((time.perf_counter_ns()-before)/1000)
    costs.append(dict(model='extra_trees_selected_INT8_C',features=5,pickle_bytes=np.nan,
                      host_predict_p50_us=float(np.percentile(latency,50)),host_predict_p99_us=float(np.percentile(latency,99)),
                      timed_calls=len(x),python_tracemalloc_peak_bytes_batch256=np.nan,
                      sha256=sha(FW/'main/components/af_model/af_model_int8.c')))
    for condition in cohort.condition.unique():
        f=load_csv(OUT/f'af_runtime_{condition}.csv')
        for mode in ['raw','confirmed']:
            rows.append(dict(dataset=f'runtime_{condition}',model=f'INT8_C_{mode}',records=f.record_id.nunique(),
                             **classification(f.label,f[mode],f.probability)))
    pd.DataFrame(rows).to_csv(OUT/'af_metrics.csv',index=False)
    results=pd.DataFrame(rows); deltas=[]
    for model in results.model.unique():
        g=results[results.model==model].set_index('dataset')
        for baseline,target in [('annotation_matched6','current_clean')]+[('current_clean','current_'+c) for c in ['ma_12dB','ma_6dB','ma_0dB']]:
            if baseline in g.index and target in g.index:
                deltas.append(dict(model=model,baseline=baseline,target=target,
                                   **{'delta_'+k:float(g.loc[target,k]-g.loc[baseline,k]) for k in ['accuracy','balanced_accuracy','sensitivity','specificity','roc_auc']}))
    pd.DataFrame(deltas).to_csv(OUT/'af_degradation.csv',index=False)
    pd.concat(predictions,ignore_index=True).to_csv(OUT/'af_predictions.csv',index=False)
    pd.DataFrame(costs).to_csv(OUT/'af_host_cost.csv',index=False)
    write_json(OUT/'af_frozen_audit.json',dict(original_annotation_reproduction=audits,
        all_original_metrics_reproduced=all(a['max_original_metric_delta']<1e-10 for a in audits),
        models=len(models),feature_sets=['all15','selected5'],refit=False,
        python=sys.version,platform=platform.platform(),timing='single predict_proba incl library overhead, excludes feature extraction; tracemalloc excludes much native memory'))

def historical_bpm():
    summary=[]; inventory=[]
    prefixes=('bpm_qrs','qtdb_qrs','nstdb_qrs','mitdb_','adaptive_','firmware_qtdb','rpeak_baseline')
    for directory in sorted(OLD.iterdir()):
        if not directory.is_dir() or not directory.name.startswith(prefixes): continue
        metrics=directory/'summary.json'; per=directory/'per_window.csv'
        config_path=next((directory/n for n in ['manifest.json','run_config.json'] if (directory/n).exists()),None)
        config=read_json(config_path) if config_path else {}
        inventory.append(dict(experiment=directory.name,path=str(directory),summary=metrics.exists(),
                              per_window=per.exists(),smoke='smoke' in directory.name,
                              protocol_path=str(config_path) if config_path else None,protocol=config,
                              status='historical, separate protocol; not automatically pooled'))
        if not per.exists(): continue
        f=load_csv(per)
        if 'bpm_abs_error' not in f: continue
        if 'tolerance_ms' not in f and 'tolerance_ms' in config:f['tolerance_ms']=config['tolerance_ms']
        groupcols=[k for k in ['detector','condition','tolerance_ms'] if k in f]
        for key,g in f.groupby(groupcols,dropna=False):
            if not isinstance(key,tuple):key=(key,)
            r=dict(experiment=directory.name,source=str(per),**dict(zip(groupcols,key)))
            r['window_sec']=config.get('window_sec',np.nan)
            r['channel_policy']=str(config.get('channel_policy',config.get('channel','unknown')))
            tpkey='tp' if 'tp' in g else 'RPeak_TP'
            if tpkey in g:
                keys=['tp','fp','fn'] if tpkey=='tp' else ['RPeak_TP','RPeak_FP','RPeak_FN']
                tp,fp,fn=[float(g[k].sum()) for k in keys]
                r.update(TP=tp,FP=fp,FN=fn,qrs_f1=2*tp/max(2*tp+fp+fn,1),
                         sensitivity=tp/max(tp+fn,1),ppv=tp/max(tp+fp,1))
            r.update(records=g.record.nunique(),**{'bpm_'+k:v for k,v in error_metrics(g.bpm_abs_error).items()})
            if 'latency_ms' in g:
                r.update(host_window_p50_ms=g.latency_ms.median(),host_window_p99_ms=g.latency_ms.quantile(.99))
            # Do not rebrand historical gap-spanning RR metric as consecutive RR MAE.
            if 'RR_MAE_ms' in g:r['legacy_matched_gap_RR_MAE_ms']=g.RR_MAE_ms.mean()
            if 'ram_peak_kib' in g:r['host_python_peak_kib']=g.ram_peak_kib.max()
            summary.append(r)
    pd.DataFrame(summary).to_csv(OUT/'bpm_all_historical_metrics.csv',index=False)
    write_json(OUT/'bpm_experiment_inventory.json',inventory)
    return inventory

def historical_af():
    rows=[]
    for cond,sub in [('clean',''),('ma_12dB','snr_12'),('ma_6dB','snr_6'),('ma_0dB','snr_0')]:
        p=ARCHIVE/'ho2025_af_downstream'/sub/'downstream_metrics.csv'
        f=load_csv(p);f.insert(0,'condition',cond);f['source']=str(p)
        # Historical implementations are NOT the later direct NeuroKit functions.
        f['gate']=f.gate.str.replace('Ho-style','Project RR agreement (Pan+XQRS)',regex=False)
        f['gate']=f.gate.str.replace('Zhao-simple','Historical Zhao approximation',regex=False)
        rows.append(f)
    pd.concat(rows,ignore_index=True).to_csv(OUT/'af_historical_gate_results.csv',index=False)

def md_table(frame,columns):
    frame=frame[columns]
    def cell(x):
        if pd.isna(x): return '—'
        if isinstance(x,(float,np.floating)): return f'{x:.4f}'
        return str(x).replace('|','/')
    return '\n'.join(['| '+' | '.join(columns)+' |','|'+'|'.join(['---']*len(columns))+'|']+
                     ['| '+' | '.join(cell(x) for x in row)+' |' for row in frame.itertuples(index=False,name=None)])

def build_report():
    hist=load_csv(OUT/'bpm_all_historical_metrics.csv'); af=load_csv(OUT/'af_metrics.csv')
    cost=load_csv(OUT/'af_host_cost.csv'); bpm=read_json(OUT/'bpm_current_metrics.json')
    cohort=load_csv(OUT/'af_cohort.csv'); audit=read_json(OUT/'af_frozen_audit.json')
    lines=['# Báo cáo đầy đủ BPM/RR và AF — 19/10/2026','',
      'Phạm vi: không chạy thêm SQI. Kết quả mới dùng detector/AF C hiện tại, mô hình AF frozen, không train lại và không tuning trên test.',
      'Artifact cũ được giữ riêng; không trộn dataset, lead, thời lượng cửa sổ hay RR annotation với RR detector.', '',
      '## 1. Những phần đã sửa/chạy bổ sung','',
      '- Chạy lại BPM/QRS/RR trên toàn bộ 13 QTDB MLII đã dùng trước đây, từ source C hiện tại.',
      '- Chạy lại ECG AFDB 6 record test × clean/MA 12/6/0 dB; tất cả 8 cấu hình AF + Extra Trees INT8 C, không SQI gate.',
      '- Chạy lại AF runtime thật (30 RR, stride15, reset RR ngoài250–2500ms; raw và xác nhận3/4) ở cả4 điều kiện.',
      '- Khôi phục các benchmark QRS/BPM trước đây và toàn bộ bảng AF/gate lịch sử; không bỏ method vì kết quả kém.',
      '- Sửa trong runner mới: đổi timestamp sang float trước khi nhân fs; đã tái hiện uint32 overflow trong builder cũ:20,000,000ms ×250/1000 ra705,032.704 thay vì5,000,000 samples. Không dùng lại bảng firmware-RR cũ làm kết quả hiện hành.',
      '- Nhãn AF mới kiểm tra toàn bộ khoảng thời gian của window, không chỉ nhãn tại31 đỉnh. RR mới giữ timestamp ms của C, không làm tròn về lưới250Hz.',
      '- RR MAE mới chỉ tính cặp kề nhau ở cả hai chuỗi; RR_MAE cũ có thể bắc qua đỉnh bị bỏ lỡ nên không gọi là RR liên tiếp.', '',
      '## 2. BPM/QRS/RR của firmware hiện tại','',
      'QTDB:13 MLII, chọn đúng channel theo header; atr là reference, 250→360Hz; AC-coupling0.5Hz mô phỏng AFE; 200counts/mV+2048; notch/QRS C. Không thêm nhiễu ở replay này. Matching one-to-one ±50ms.',
      'BPM display là trường bpm thật của firmware, cập nhật5s. Reference=60000/mean RR có đầu phải nằm trong5s trước mốc đọc. Giá trị0 tính là không có dự đoán; coverage luôn đi kèm sai số.', '',
      '| Metric | Giá trị |','|---|---:|',
      f"| QRS TP / FP / FN | {bpm['TP']} / {bpm['FP']} / {bpm['FN']} |",
      f"| QRS sensitivity / PPV / F1 | {bpm['sensitivity']:.6f} / {bpm['ppv']:.6f} / {bpm['f1']:.6f} |",
      f"| Event accuracy TP/(TP+FP+FN), không phải BPM accuracy | {bpm['event_accuracy']:.6f} |",
      f"| BPM display MAE / RMSE / p95 abs (BPM) | {bpm['display_bpm']['mae']:.4f} / {bpm['display_bpm']['rmse']:.4f} / {bpm['display_bpm']['p95_abs']:.4f} |",
      f"| BPM display số mốc / coverage | {bpm['display_bpm']['total']} / {bpm['display_bpm']['coverage']:.2%} |",
      f"| BPM trong ±5 BPM / toàn bộ mốc | {bpm['display_bpm']['within_5_all']:.2%} |",
      f"| RR liên tiếp MAE / RMSE (ms), conditional | {bpm['adjacent_rr']['mae']:.4f} / {bpm['adjacent_rr']['rmse']:.4f} |",
      f"| Cặp RR liên tiếp matched / coverage reference RR | {bpm['adjacent_rr']['valid']} / {bpm['adjacent_rr_reference_coverage']:.2%} |",'',
      '±5 BPM là tolerance báo cáo của dự án, không phải một chuẩn chứng nhận. RR conditional không bao gồm interval mất/extra peak; xem QRS FP/FN và coverage.', '',
      '## 3. Toàn bộ benchmark BPM/QRS lịch sử','',
      f"Có {hist.detector.nunique()} tên detector, {hist.experiment.nunique()} run có per-window, {len(hist)} dòng tổng hợp. Bảng đầy đủ: [bpm_all_historical_metrics.csv](bpm_all_historical_metrics.csv). Danh mục cả run không có per-window: [bpm_experiment_inventory.json](bpm_experiment_inventory.json).",
      'BPM lịch sử là estimator offline theo từng window, không phải trường display5s. Host latency không phải ESP32. Tolerance khác nhau được giữ riêng; smoke và benchmark không được gộp.', '']
    for exp in ['qtdb_qrs_ceiling','mitdb_all_detectors','nstdb_qrs_ceiling','bpm_qrs_classic_benchmark','bpm_qrs_sota_benchmark','adaptive_int16_qtdb_locked','adaptive_int16_nstdb']:
        f=hist[hist.experiment==exp]
        if 'tolerance_ms' in f and (f.tolerance_ms==50).any():f=f[f.tolerance_ms==50]
        if len(f):
            cols=['detector']+(['condition'] if f.condition.notna().any() else [])+['tolerance_ms','window_sec','records','bpm_total','qrs_f1','bpm_mae','bpm_rmse','bpm_within_5_all','bpm_coverage','host_window_p50_ms','host_window_p99_ms']
            lines+=['### '+exp,'',md_table(f,cols),'']
    lines+=['## 4. AF frozen — tất cả mô hình, không gate','',
      '4 họ mô hình × all15/selected5: Logistic Regression, Random Forest, Extra Trees, XGBoost. Thêm Extra Trees INT8 C. Selected5=pnn50, shannon_entropy, sd2_ms, sd1_sd2, median_rr_ms.',
      'Split gốc record-disjoint:18 train/58,727 windows và7 test/20,194 windows annotation. 00735 không có waveform nên replay ECG dùng6 record test. Không fit lại, không chọn model trên test.',
      f"Kiểm tra lại8 kết quả annotation gốc: {'khớp toàn bộ (max delta<1e-10)' if audit['all_original_metrics_reproduced'] else 'có chênh lệch; xem af_frozen_audit.json'}.",
      'Runtime raw/confirmed dùng cùng score liên tục nên AUC trong cả hai dòng là AUC raw score, không phải AUC của bộ xác nhận3/4.',
      'Mỗi điều kiện waveform tạo cửa sổ theo peak của chính điều kiện đó; số cửa sổ có thể khác nhau. Vì vậy suy hao giữa noise levels là cohort-level, không phải paired-window causal effect. Không công bố coverage100% theo thời gian chỉ vì không có SQI gate.', '']
    for dataset,g in af.groupby('dataset',sort=False):
        lines+=['### '+dataset,'',md_table(g,['model','records','windows','accuracy','balanced_accuracy','sensitivity','specificity','f1','roc_auc','tn','fp','fn','tp']),'']
    lines+=['### Số mẫu/nhãn từng task AF','',md_table(load_csv(OUT/'dataset_counts.csv'),['dataset','records','windows','af','non_af']),'',
            '### Suy hao từng mô hình (target − baseline)','',
            md_table(load_csv(OUT/'af_degradation.csv'),['model','baseline','target','delta_balanced_accuracy','delta_sensitivity','delta_specificity','delta_roc_auc']),'']
    coverage=cohort.groupby('condition',sort=False)[['candidate','invalid_rr','mixed_unknown','valid','runtime_emitted']].sum().reset_index()
    coverage['valid_candidate_fraction']=coverage.valid/coverage.candidate
    lines+=['### Coverage / loại cửa sổ','',md_table(coverage,list(coverage.columns)),'',
      'candidate: cửa sổ31 peak, stride15 trước lọc. invalid_rr: có RR ngoài250–2500ms. mixed_unknown: chuyển AF/nonAF hoặc unknown trên toàn khoảng. Đây không phải coverage theo thời gian; valid runtime có lịch phát khác sau reset.', '',
      '## 5. Thời gian và dung lượng AF','',
      'Đo mới host:200 lần predict_proba một cửa sổ/model, warm-up trước; INT8 dùng ctypes→C. Bao gồm overhead gọi, chưa gồm feature extraction. Pickle là artifact máy tính, không phải Flash Δ. tracemalloc batch256 không bao quát native allocations, không phải RAM ESP32.', '',
      md_table(cost,['model','features','pickle_bytes','host_predict_p50_us','host_predict_p99_us','python_tracemalloc_peak_bytes_batch256']), '',
      'Bằng chứng INT8 triển khai lưu trước đây:80 cây,17,712 nodes; constants141,881B; Xtensa object142,135B; writable static RAM0B; max function stack48B. 13B chỉ là explicit working values của inference API, không phải tổng RAM AF/pipeline. Host C1.880µs/window là số cũ, không phải latency ESP32.',
      f"Nguồn: `{OLD/'af_detection/int8_deployment/REPORT.md'}` và object/stack-usage cùng thư mục.",
      'Pipeline ESP32 capture cũ:27,975/27,975 mẫu; p99≤250µs (histogram bound), max3337µs > period2770µs; SD/BLE drops0. Đây là toàn pipeline, không được gán thành latency riêng của AF.',
      r'Nguồn: `D:\DOC\ECG_PAPER\temp\out\pipeline_profile_assessment.md`. Chưa chạy/flash board lại trong lượt này; p99 AF riêng và Flash/RAM linked delta của các model chưa port vẫn chưa có.', '',
      '## 6. AF + gate lịch sử — giữ đủ, không đánh tráo tên','',
      'Toàn bộ số đã có: [af_historical_gate_results.csv](af_historical_gate_results.csv). Không chạy thêm SQI. Ho-style cũ dùng Pan+XQRS và tỷ lệRR, không phải Ho[NeuroKit] (UNSW+NeuroKit, tỷ lệsample). Zhao-simple cũ trong run này là approximation, không phải direct Zhao[NeuroKit]. Các bảng này chỉ là historical downstream, không thay thế rerun hiện tại.', '',
      md_table(load_csv(OUT/'af_historical_gate_results.csv'),['condition','model','gate','windows','coverage','af_coverage','balanced_accuracy','sensitivity','specificity','roc_auc']), '',
      '## 7. Giới hạn và checklist còn lại','',
      '- [x] Báo đủ các họ AF đã có artifact; rerun8 float configurations và INT8 trên annotation/current waveform.',
      '- [x] Bổ sung BPM accuracy-within-tolerance, MAE/RMSE, RR conditional với coverage và QRS counts.',
      '- [x] Replay current C trên clean/12/6/0dB, không SQI.',
      '- [x] Tách model-size host / Xtensa object / working RAM / toàn pipeline.',
      '- [ ] Chưa có AF event-level sensitivity, false alarms/hour, onset delay; window metrics không thay thế các metric này.',
      '- [ ] Chưa có p99 AF riêng đo mới trên ESP32 hoặc đo từng float model trên board; không tự flash thiết bị.',
      '- [ ] Chưa clinical/external patient validation; dữ liệu AFDB có thể đã được quan sát nhiều lần trong phát triển nên không coi là prospective test.',
      '- [ ] So sánh noise chính xác theo cùng khoảng thời gian cần fixed-time evaluation khác beat-count protocol hiện tại; không diễn giải BA tăng khi nhiễu là mô hình tốt lên.', '',
      '## 8. Tái chạy','',
      '```powershell',r'$env:PYTHONDONTWRITEBYTECODE="1"',
      r'D:\DOC\ECG_Diagnosis_System\.venv\Scripts\python.exe tools\complete_bpm_af_report.py --phase all','```','',
      'Source/hash/protocol: [replay_manifest.json](replay_manifest.json), [af_frozen_audit.json](af_frozen_audit.json). Predictions từng cửa sổ: [af_predictions.csv](af_predictions.csv). Không có SQI calls trong adapter C.']
    (OUT/'REPORT_BPM_AF.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--phase',choices=['all','replay','evaluate','report'],default='all')
    args=parser.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    start=time.perf_counter()
    lib=compile_host() if args.phase in ['all','replay','evaluate'] else None
    if args.phase in ['all','replay']: run_replay(lib)
    if args.phase in ['all','evaluate']: evaluate_models(lib)
    if args.phase in ['all','report']:
        historical_bpm();historical_af();build_report()
    write_json(OUT/'last_run.json',dict(phase=args.phase,elapsed_seconds=time.perf_counter()-start,completed=True))
    print('DONE',args.phase,round(time.perf_counter()-start,2),flush=True)

if __name__=='__main__':main()
