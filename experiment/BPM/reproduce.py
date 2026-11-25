"""Reproduce audited BPM results; fresh inference requires --inference."""
import argparse,json,sys
from datetime import datetime, timezone
import time
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from BPM.utils.reproducibility import SEED, lock_random_seed
if __name__ == '__main__':
    lock_random_seed(restart_for_hash=True)
import numpy as np
import pandas as pd
from BPM.utils._shared import ROOT,registry,save,sha,verify_manifest,fresh_output,plot
from BPM import metrics
from BPM.modules import _runtime as runtime

def main(argv=None):
    lock_random_seed()
    methods=registry('BPM');p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,default=ROOT/'datasets/prepared/BPM/manifest.json')
    p.add_argument('--methods',nargs='+',choices=list(methods));p.add_argument('--backend',choices=['host','c','both'],default='both')
    p.add_argument('--inference',action='store_true',help='Run fresh inference instead of the audited reproduction cache')
    p.add_argument('--from-cache',action='store_true',default=True,help='Reproduce audited complete metrics without running detectors')
    p.add_argument('--report',action='store_true',help='Reproduce the complete locked report table and refresh Markdown; no inference')
    p.add_argument('--records',nargs='+');p.add_argument('--conditions',nargs='+',choices=runtime.CONDITIONS)
    p.add_argument('--output',type=Path,default=ROOT/'BPM/results'/datetime.now(timezone.utc).strftime('reproduce_%Y%m%dT%H%M%S_%fZ'));a=p.parse_args(argv)
    a.from_cache=not a.inference
    if a.report and a.inference:p.error('--report cannot run fresh inference')
    if not a.inference:a.report=True
    if a.backend=='both':
        if a.inference:p.error('Fresh inference requires --backend host or c')
        if a.methods or a.records or a.conditions:p.error('Combined reproduction requires the complete cohort')
        parent=fresh_output(a.output)
        try:
            main(['--backend','host','--output',str(parent/'python')])
            main(['--backend','c','--output',str(parent/'esp32')])
            save(parent/'status.json',dict(state='complete',backends=['host','c']))
        except BaseException as error:
            save(parent/'failure.json',dict(state='failed',reason=str(error)));raise
        return
    if a.report:
        if a.records or a.conditions or a.methods:p.error('--report requires the complete locked method/cohort selection')
        a.from_cache=True
    a.methods=a.methods or [k for k,m in methods.items() if a.backend=='host' or hasattr(m,'C_PORT')]
    out=fresh_output(a.output);raw_rows=[]
    try:
        if a.from_cache:
            if a.records or a.conditions:raise ValueError('Cache replay uses complete cohort, no subsets')
            cells=pd.concat([runtime.cache_c_cells(k) if a.backend=='c' else runtime.cache_cells(k,methods[k].CACHE) for k in a.methods],ignore_index=True)
        else:
            import wfdb
            m=verify_manifest(a.manifest,'BPM');records=a.records or m['records'];conditions=a.conditions or m['conditions']
            if not set(records)<=set(m['records']):raise ValueError('Excluded record requested')
            (out/'predictions').mkdir();detectors={}
            for method in a.methods:
                detectors[method]=runtime.c_detector(method,out/'build') if a.backend=='c' else methods[method].load(out/'build')
            noise_cache={}
            for condition in conditions:
                for record in records:
                    wave=wfdb.rdrecord(str(ROOT/m['data']/record),channels=[0]);ann=wfdb.rdann(str(ROOT/m['data']/record),'atr')
                    assert wave.fs==250 and wave.units==['mV']
                    ref=np.array([s*1000/wave.fs for s,k in zip(ann.sample,ann.symbol) if k in runtime.BEATS]);raw=wave.p_signal[:,0]
                    if condition!='original':
                        kind,level=condition.split('_')
                        if kind not in noise_cache:
                            noise,meta=wfdb.rdsamp(str(ROOT/'datasets/nstdb'/kind),channels=[0]);noise_cache[kind]=(noise[:,0],meta['fs'])
                        noise,nfs=noise_cache[kind]
                        raw,_=runtime.mixed_bursts(raw,wave.fs,noise,nfs,record) if level=='mixed' else runtime.mix_burst(raw,wave.fs,noise,nfs,int(level),record)
                    adc,_=runtime.adc_from_mv(raw,wave.fs);x=(adc.astype(float)-2048)/200
                    for method,detect in detectors.items():
                        begin=time.perf_counter()
                        if a.backend=='c':
                            bpm,local,counts=detect(x);ends=np.arange(len(bpm))*5000+10000
                            mask=(np.arange(128)[None,:]<counts[:,None])&(local>=1800)&(local<3600)
                            pred=(local*(1000/360)+(ends-10000)[:,None])[mask]
                            # QRS first5s is not scored twice or backfilled from later windows.
                            scored_ref=ref[(ref>=5000)&(ref<ends[-1])]
                        elif method=='firmware_C':pred,ends,bpm=detect(adc);scored_ref=ref
                        else:
                            indices=np.asarray(detect(x),float)
                            if indices.ndim!=1 or not np.isfinite(indices).all() or np.any(indices<0) or np.any(indices>=len(x)):raise ValueError('Invalid detector indices')
                            pred=np.sort(indices,kind='stable')*1000/360;ends=np.arange(5000,(len(x)-1)*1000/360+.01,5000)
                            bpm=metrics.bpm_from_peaks(pred,ends);scored_ref=ref
                        arrays=dict(reference_ms=scored_ref,prediction_ms=pred,bpm_end_ms=ends,bpm_values=bpm)
                        if a.backend=='c':arrays['reference_bpm']=metrics.reference_bpm_context(ref,ends)
                        elapsed=time.perf_counter()-begin
                        np.savez_compressed(out/'predictions'/f'{method}_{record}_{condition}.npz',**arrays)
                        for row in metrics.score(arrays):raw_rows.append(dict(method=method,record=record,condition=condition,host_seconds=elapsed,**row))
                        save(out/'status.json',dict(state='running',completed_units=len(raw_rows)//2,record=record,condition=condition,method=method))
                        print(method,record,condition,flush=True)
            pd.DataFrame(raw_rows).to_csv(out/'record_metrics.csv',index=False)
            cells=metrics.aggregate(pd.DataFrame(raw_rows))
        cells.to_csv(out/'condition_metrics.csv',index=False);summary=metrics.summarize(cells);summary.to_csv(out/'metrics_raw.csv',index=False)
        figures=out/'figures'
        plot(summary,figures/'rmse.png','BPM: mean condition RMSE',ylabel='BPM RMSE')
        plot(summary,figures/'f1_150.png','R-peak F1 at ±150 ms',y='f1_150',ylabel='F1')
        if not a.report:summary.to_csv(out/'metrics.csv',index=False)
        if a.report:
            from BPM.utils._summary import refresh
            refresh(verification=('BPM',a.backend,summary),output=out)
        save(out/'protocol.json',dict(seed=SEED,aspect='BPM',backend=a.backend,cache_replay=a.from_cache,report_reproduction=a.report,methods=a.methods,
            scope='full-record host' if a.backend=='host' else 'reset10s/context;hop5s;last5s QRS only',
            diagnostic_subset=bool(a.records or a.conditions),source_sha256={str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__),Path(metrics.__file__),*[Path(methods[k].__file__) for k in a.methods]]}))
        save(out/'status.json',dict(state='complete',methods=len(summary),figures=str(figures.relative_to(ROOT))))
        print(summary.to_string(index=False))
    except BaseException as e:save(out/'failure.json',dict(state='failed',reason=str(e)));raise

if __name__=='__main__':main()
