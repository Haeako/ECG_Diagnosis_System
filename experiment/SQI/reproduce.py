"""Reproduce audited SQI results; fresh inference requires --inference."""
import argparse,json,sys
from datetime import datetime, timezone
import time
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from SQI.utils.reproducibility import SEED, lock_random_seed
if __name__ == '__main__':
    lock_random_seed(restart_for_hash=True)
import numpy as np
import pandas as pd
from SQI.utils._shared import ROOT,registry,fresh_output,verify_manifest,save,plot,sha
from SQI import metrics
from SQI.modules import _runtime

def main(argv=None):
    lock_random_seed()
    methods=registry('SQI');p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,default=ROOT/'datasets/prepared/SQI/manifest.json')
    p.add_argument('--methods',nargs='+',choices=list(methods),default=list(methods))
    p.add_argument('--inference',action='store_true',help='Run fresh inference instead of the audited reproduction cache')
    p.add_argument('--from-cache',action='store_true',default=True,help='Recompute metrics/plots from frozen audited scores, no inference')
    p.add_argument('--report',action='store_true',help='Reproduce all nine locked SQI methods and refresh Markdown; no inference')
    p.add_argument('--limit',type=int,help='Diagnostic window subset, never a full-cohort result')
    p.add_argument('--output',type=Path,default=ROOT/'SQI/results'/datetime.now(timezone.utc).strftime('reproduce_%Y%m%dT%H%M%S_%fZ'));a=p.parse_args(argv)
    a.from_cache=not a.inference
    if a.report and a.inference:p.error('--report cannot run fresh inference')
    if not a.inference:a.report=True
    out=fresh_output(a.output)
    try:
        if a.report:
            if a.limit or set(a.methods)!=set(methods):raise ValueError('--report requires all methods and the complete cohort')
            a.from_cache=True
        if a.limit is not None and a.limit<1:raise ValueError('--limit must be positive')
        if a.from_cache:
            labels=pd.read_csv(ROOT/'report/benchmark_runs/butqdb_20261020/windows.csv',dtype={'record_id':str})
            scores=pd.concat([_runtime.cached(methods[k]) for k in a.methods],ignore_index=True)
            if a.limit:labels=labels.head(a.limit);scores=scores[scores.window_id.isin(labels.window_id)]
        else:
            m=verify_manifest(a.manifest,'SQI');labels=pd.read_csv(ROOT/m['labels'],dtype={'record_id':str})
            if a.limit:labels=labels.head(a.limit)
            calls={k:methods[k].load(out/'build') for k in a.methods};rows=[]
            need_primary=any(k in a.methods for k in ('bSQI C','Orphanidou external C','Zhao shared-peak C'))
            for record,g in labels.groupby('record_id',sort=False):
                raw=np.load(ROOT/m['data']/f'{record}_ECG_ecg.npy',mmap_mode='r')
                events=_runtime.events(record) if need_primary else None
                for item in g.to_dict('records'):
                    window=_runtime.Window(raw,item,events)
                    for method,call in calls.items():
                        begin=time.perf_counter();reason=''
                        try:score=call(window)
                        except (ValueError,IndexError,ZeroDivisionError,FloatingPointError) as e:score=np.nan;reason=str(e)
                        rows.append(dict(window_id=item['window_id'],method=method,score=score,invalid_reason=reason,host_ms=(time.perf_counter()-begin)*1000))
                    if len(rows)%(100*len(calls))==0:save(out/'status.json',dict(state='running',windows_done=len(rows)//len(calls),windows_total=len(labels)))
                print(record,len(g),'windows',flush=True)
            scores=pd.DataFrame(rows)
        assert len(scores)==len(labels)*len(a.methods)
        scores.to_csv(out/'predictions.csv',index=False);summary=metrics.summarize(scores,labels);summary.to_csv(out/'metrics_raw.csv',index=False)
        figures=out/'figures'
        plot(summary,figures/'auc_common.png','SQI AUC: common valid cohort',y='auc_common',ylabel='AUC')
        plot(summary,figures/'coverage.png','SQI score coverage',y='coverage',ylabel='Coverage')
        if not a.report:summary.to_csv(out/'metrics.csv',index=False)
        if a.report:
            from SQI.utils._summary import refresh
            refresh(verification=('SQI',None,summary),output=out)
        save(out/'protocol.json',dict(seed=SEED,aspect='SQI',methods=a.methods,cache_replay=a.from_cache,report_reproduction=a.report,diagnostic_subset=bool(a.limit),
            train_or_tune=False,source_sha256={str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__),Path(metrics.__file__),*[Path(methods[k].__file__) for k in a.methods]]}))
        save(out/'status.json',dict(state='complete',windows=len(labels),methods=len(a.methods),figures=str(figures.relative_to(ROOT))))
        print(summary.to_string(index=False))
    except BaseException as e:save(out/'failure.json',dict(state='failed',reason=str(e)));raise

if __name__=='__main__':main()
