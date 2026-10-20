"""Subject-disjoint SQI survey on the user-selected BUTQDB HF snapshot.

Never modifies source data. No invented beat/rhythm labels or 12-lead features.
"""
from __future__ import annotations
import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import sys
import time
import warnings
from concurrent.futures import ProcessPoolExecutor

FW=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(FW/'.cache/butqdb_deps'))
os.environ.setdefault('OMP_NUM_THREADS','1')
os.environ.setdefault('OPENBLAS_NUM_THREADS','1')
os.environ.setdefault('MKL_NUM_THREADS','1')
os.environ.setdefault('MPLCONFIGDIR',str(FW/'.cache/matplotlib'))
sys.dont_write_bytecode=True
import numpy as np
import pandas as pd
from scipy import signal
from sklearn.model_selection import StratifiedGroupKFold

DATA=FW.parents[1]/'butqdb'
OUT=FW/'benchmark_runs/butqdb_20261020'
ARCHIVE=Path(r'D:\DOC\ECG_PAPER\temp\benchmark_results')
WINDOW=10000

def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for x in iter(lambda:f.read(8*1024*1024),b''):h.update(x)
    return h.hexdigest()

def save_json(path,obj):Path(path).write_text(json.dumps(obj,indent=2,ensure_ascii=False,default=str),encoding='utf-8')
def read_frame(path):return pd.read_csv(path,dtype={'record_id':str,'subject_id':str},float_precision='round_trip')

def prepare():
    OUT.mkdir(parents=True,exist_ok=True)
    table=pd.read_parquet(DATA/'data/train-00000-of-00001.parquet')
    rows=[];inventory=[];intervals=[];source_hashes={}; exclusions=[]
    for record in table.itertuples():
        header=json.loads(record.ecg_header);acc_header=json.loads(record.acc_header)
        name=header['record_name'].split('_')[0]
        path=DATA/'data'/Path(record.ecg_signal_url).name
        accpath=DATA/'data'/Path(record.acc_signal_url).name
        x=np.load(path,mmap_mode='r',allow_pickle=False)
        acc=np.load(accpath,mmap_mode='r',allow_pickle=False)
        assert header['fs']==1000 and header['n_sig']==1 and header['units']==['uV'],header
        assert x.shape==(header['sig_len'],1),(name,x.shape,header)
        assert acc.shape==(acc_header['sig_len'],3) and acc_header['fs']==100
        nonfinite=sum(int((~np.isfinite(x[a:a+1_000_000])).sum()) for a in range(0,len(x),1_000_000))
        if nonfinite:raise ValueError(f'{name}: {nonfinite} nonfinite waveform samples; stop, no imputation')
        ann=pd.DataFrame(record.annotations)
        assert set(ann.annotator)=={1,2,3,4}
        c=ann[ann.annotator==4].sort_values('start').copy()
        assert np.isfinite(c[['start','end','value']]).all().all()
        assert ((c[['start','end','value']]%1)==0).all().all()
        c=c.astype(int)
        # Original ann_reader.m is MATLAB 1-based, inclusive start/end.
        starts=c.start.to_numpy()-1;ends=c.end.to_numpy();labels=c.value.to_numpy()
        assert starts[0]==0 and ends[-1]==len(x)
        assert np.array_equal(starts[1:],ends[:-1]),f'{name}: annotation gap/overlap'
        assert np.all(ends>starts) and set(labels)<={0,1,2,3}
        n=len(x)//WINDOW
        counts=np.zeros((n,4),dtype=np.int32)
        for a,b,k in zip(starts,ends,labels):
            intervals.append(dict(record_id=name,subject_id=name[:3],start_sample=int(a),end_exclusive=int(b),quality=int(k)))
            first=a//WINDOW;last=(b-1)//WINDOW
            for w in range(first,min(last+1,n)):
                counts[w,k]+=min(b,(w+1)*WINDOW)-max(a,w*WINDOW)
        assert np.all(counts.sum(axis=1)==WINDOW)
        for i,values in enumerate(counts):
            if values[0]:continue  # Unknown is not good, bad, or imputed.
            worst=int(np.flatnonzero(values)[-1])
            rows.append(dict(window_id=f'{name}_{i:06d}',record_id=name,subject_id=name[:3],
                             start_sample=i*WINDOW,end_sample=(i+1)*WINDOW,quality_worst=worst,
                             class1_fraction=values[1]/WINDOW,class2_fraction=values[2]/WINDOW,class3_fraction=values[3]/WINDOW,
                             mixed_classes=int(np.count_nonzero(values)>1),qrs_target=int(values[3]==0),
                             morphology_target=int(values[1]==WINDOW)))
        source_hashes[str(path)]=sha(path);source_hashes[str(accpath)]=sha(accpath)
        inventory.append(dict(record_id=name,subject_id=name[:3],ecg_path=str(path),acc_path=str(accpath),
                              fs=header['fs'],samples=len(x),hours=len(x)/1000/3600,dtype=str(x.dtype),units='uV',
                              windows_total=n,windows_labeled=int(np.sum(counts[:,0]==0)),
                              windows_unknown=int(np.sum(counts[:,0]>0)),trailing_samples=len(x)%WINDOW,
                              **{f'class{k}_seconds':int(np.sum((ends-starts)[labels==k]))/1000 for k in range(4)}))
        print('PREPARE',name,'fully labeled windows',int(np.sum(counts[:,0]==0)),flush=True)
    windows=pd.DataFrame(rows).sort_values(['record_id','start_sample']).reset_index(drop=True)
    assert windows.window_id.is_unique and windows.subject_id.nunique()==15
    # One frozen outer partition for all methods AND both tasks, grouping subjects.
    splitter=StratifiedGroupKFold(5,shuffle=True,random_state=42)
    windows['fold']=-1
    folds=[]
    for fold,(train,test) in enumerate(splitter.split(windows,windows.quality_worst,windows.subject_id)):
        assert not set(windows.iloc[train].subject_id)&set(windows.iloc[test].subject_id)
        for target in ['qrs_target','morphology_target']:
            assert windows.iloc[train][target].nunique()==2,f'train missing class {target}/{fold}'
        windows.loc[test,'fold']=fold
        folds.append(dict(fold=fold,train_subjects=sorted(windows.iloc[train].subject_id.unique()),
                          test_subjects=sorted(windows.iloc[test].subject_id.unique()),train_windows=len(train),test_windows=len(test)))
    windows.to_csv(OUT/'windows.csv',index=False)
    pd.DataFrame(inventory).to_csv(OUT/'dataset_inventory.csv',index=False)
    pd.DataFrame(intervals).to_csv(OUT/'consensus_intervals.csv',index=False)
    source_hashes[str(DATA/'data/train-00000-of-00001.parquet')]=sha(DATA/'data/train-00000-of-00001.parquet')
    save_json(OUT/'protocol.json',dict(dataset=str(DATA),hf_revision='891cd492b71ee796c05ae8398ed913614c66ca91',
        windows=len(windows),records=18,subjects=15,consensus_annotator=4,annotation_indexing='1-based inclusive -> [start-1,end)',
        window_sec=10,stride_sec=10,window_grid='record-start anchored; full 10-second windows only',
        unknown_policy='exclude entire window if any sample has consensus0; no inference on unannotated data',
        primary_target='QRS usability: no class3 anywhere in window (class1+2 good vs any3 bad)',
        secondary_target='Morphology: all samples class1 good; any2/3 bad',
        window_aggregation='conservative worst consensus class; project protocol, not paper original',
        sensitivity_analysis='report class fractions and pure/mixed strata; no relabeling based on method outputs',
        preprocessing='float16 physical uV cast float64 then /1000 to mV; polyphase1000->500->125 with 1s real context each side cropped off; line padding only at recording edges; no added noise, no blanket denoising',
        folds=folds,seed=42,subject_id='first3digits per PhysioNet documentation',
        calibration='none for fixed rules; learned models refit on train subjects; no test threshold tuning',
        limitations=['HF snapshot waveforms are float16 converted, not bit-identical WFDB originals',
                     'quality labels are not beat timestamps or AF rhythm labels; no supervised BPM/AF accuracy on this dataset',
                     'single ECG lead; Clifford84 and cross-lead iSQI cannot be evaluated'],
        source_hashes=source_hashes,source_url='https://physionet.org/content/butqdb/1.0.0/'))
    print(windows.groupby(['fold','quality_worst']).size().to_string(),flush=True)

def init_worker():
    global nk,ref,rules,ep,wqrs,arrays
    import run_cinc2011_verified as prior
    prior.initialize(ARCHIVE)
    nk,ref,rules,ep,wqrs=prior.nk,prior.ref,prior.rules,prior.ep,prior.wqrs
    arrays={}

def extract(row):
    started=time.perf_counter()
    name=row['record_id']
    if name not in arrays:arrays[name]=np.load(DATA/f'data/{name}_ECG_ecg.npy',mmap_mode='r',allow_pickle=False)
    a=max(0,row['start_sample']-1000);b=min(len(arrays[name]),row['end_sample']+1000)
    raw=np.asarray(arrays[name][a:b,0],dtype=float)/1000
    # Real neighboring samples avoid manufacturing edge transients from the
    # substantial DC offset in some records. Context never enters scored masks.
    context500=signal.resample_poly(raw,1,2,padtype='line')
    context125=signal.resample_poly(context500,1,4,padtype='line')
    offset=row['start_sample']-a
    raw500=context500[offset//2:offset//2+5000]
    x=context125[offset//8:offset//8+1250]
    out=dict(window_id=row['window_id'])
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter('always')
        t=time.perf_counter();primary=ep.detect(x);out['ep_seconds']=time.perf_counter()-t
        t=time.perf_counter();secondary=np.asarray(wqrs.wqrs_detector(x),int);out['wqrs_seconds']=time.perf_counter()-t
        t=time.perf_counter();out.update(ref.per_lead_features(x,primary,secondary));out['features_seconds']=time.perf_counter()-t
        count=ref.ordered_matches(primary,secondary,round(.05*125));total=len(primary)+len(secondary)
        out['bsqi_f1']=2*count/total if total else 0.
        t=time.perf_counter();out['orphanidou_accept'],out['orphanidou_score']=rules.orphanidou_rule(x,125,primary);out['orphanidou_seconds']=time.perf_counter()-t
        t=time.perf_counter();out['kramer_accept'],out['kramer_score']=rules.kramer_rule(raw500);out['kramer_seconds']=time.perf_counter()-t
        t=time.perf_counter();z=nk.ecg_quality(raw500,sampling_rate=500,method='zhao2018',approach='simple');out['zhao_seconds']=time.perf_counter()-t
        mapping={'Unacceptable':0.,'Barely acceptable':.5,'Excellent':1.}
        if z not in mapping:raise ValueError(f'{row["window_id"]}: unexpected Zhao result')
        out.update(zhao_score=mapping[z],zhao_accept=int(z!='Unacceptable'),zhao_label=z)
        t=time.perf_counter();q=np.asarray(nk.ecg_quality(raw500,sampling_rate=500,method='ho2025'));out['ho_seconds']=time.perf_counter()-t
        if q.shape!=raw500.shape or not np.isin(q,[0,1]).all():raise ValueError('Ho invalid output')
        out['ho_score']=float(q.mean())
        centered=x-x.mean();rms=np.sqrt(np.mean(centered**2))
        out['normalized_diff_rms']=float(np.sqrt(np.mean(np.diff(centered)**2))/rms) if rms else np.nan
        f,p=signal.welch(centered,fs=125,nperseg=250);p=p[(f>=.5)&(f<=40)]
        total_power=p.sum();p=p/total_power if total_power else np.zeros_like(p)
        out['spectral_entropy']=float(-np.sum(p*np.log(p+np.finfo(float).eps))/np.log(len(p))) if total_power else np.nan
        rr=np.diff(primary)*8.;median=np.median(rr) if len(rr) else np.nan
        out['rr_outlier_fraction']=float(np.mean((rr<300)|(rr>2000)|(rr<.5*median)|(rr>1.8*median))) if len(rr) else np.nan
        beats=[]
        for peak in primary:
            if peak>=15 and peak+26<=len(x):
                b=x[peak-15:peak+26];b=b-b.mean();norm=np.linalg.norm(b)
                if norm:beats.append(b/norm)
        out['template_corr']=np.nan
        if len(beats)>=3:
            m=np.asarray(beats);template=np.median(m,axis=0);norm=np.linalg.norm(template)
            if norm:out['template_corr']=float(np.mean(m@(template/norm)))
        out['warnings']=json.dumps(sorted(set(str(w.message) for w in caught)))
    out['elapsed_seconds']=time.perf_counter()-started
    return out

def extraction(workers,smoke=False):
    windows=read_frame(OUT/'windows.csv')
    if smoke:
        windows=windows.groupby(['record_id','quality_worst']).head(2).reset_index(drop=True)
    target=OUT/('smoke_features.csv' if smoke else 'features.csv')
    done=set()
    if target.exists():done=set(read_frame(target).window_id)
    pending=windows[~windows.window_id.isin(done)]
    print('EXTRACT',len(pending),'pending;',len(done),'cached',flush=True)
    try:
        with ProcessPoolExecutor(max_workers=workers,initializer=init_worker) as pool:
            for i,result in enumerate(pool.map(extract,pending.to_dict('records'),chunksize=1),1):
                pd.DataFrame([result]).to_csv(target,mode='a',header=not target.exists(),index=False)
                if i%100==0 or i==len(pending):print('EXTRACT',len(done)+i,'/',len(windows),flush=True)
    except Exception as exc:
        save_json(OUT/'extraction_failure.json',dict(error=repr(exc),smoke=smoke,stop_on_failure=True))
        raise

def main():
    p=argparse.ArgumentParser();p.add_argument('--phase',choices=['prepare','smoke','extract'],required=True)
    p.add_argument('--workers',type=int,default=4);args=p.parse_args()
    if args.phase=='prepare':prepare()
    else:extraction(args.workers,args.phase=='smoke')

if __name__=='__main__':main()
