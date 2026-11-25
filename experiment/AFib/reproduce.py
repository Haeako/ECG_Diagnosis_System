"""Reproduce audited AFib results; fresh inference requires --inference."""
import argparse,json,sys
from datetime import datetime, timezone
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from AFib.utils.reproducibility import SEED, lock_random_seed
if __name__ == '__main__':
    lock_random_seed(restart_for_hash=True)
import numpy as np
import pandas as pd
from AFib.utils._shared import ROOT,registry,fresh_output,verify_manifest,sha,checked,save,plot
from AFib import metrics

def main(argv=None):
    lock_random_seed()
    methods=registry('AFib');p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,default=ROOT/'datasets/prepared/AFib/manifest.json')
    p.add_argument('--methods',nargs='+',choices=list(methods),default=list(methods));p.add_argument('--output',type=Path,default=ROOT/'AFib/results'/datetime.now(timezone.utc).strftime('reproduce_%Y%m%dT%H%M%S_%fZ'))
    p.add_argument('--inference',action='store_true',help='Run fresh inference instead of the audited reproduction cache')
    p.add_argument('--from-cache',action='store_true',default=True,help='Replay audited final probabilities; no inference or training')
    p.add_argument('--report',action='store_true',help='Reproduce all eight locked AF methods and refresh Markdown; no inference or training')
    p.add_argument('--limit',type=int,help='Diagnostic common-window subset');a=p.parse_args(argv)
    a.from_cache=not a.inference
    if a.report and a.inference:p.error('--report cannot run fresh inference')
    if not a.inference:a.report=True
    out=fresh_output(a.output)
    try:
        if a.report:
            if a.limit or set(a.methods)!=set(methods):raise ValueError('--report requires the complete frozen cohort/methods and no training')
            a.from_cache=True
        if a.limit is not None and a.limit<1:raise ValueError('--limit must be positive')
        folder=ROOT/'outputs/AFib/afdb_frozen_full_20261118';audit=json.loads((folder/'publication_audit.json').read_text())
        assert audit['status']=='PASS';path=checked(folder/'predictions.csv',audit['predictions_sha256'])
        frame=pd.read_csv(path,dtype={'record':str},low_memory=False);population=int(frame.pure_label.sum())
        frame=frame[frame.common_valid].copy();assert len(frame)==18470
        if a.limit:frame=frame.head(a.limit)
        if not a.from_cache:m=verify_manifest(a.manifest,'AFib')
        fusion_frame=None;corrected={};rows=[]
        for method in a.methods:
            module=methods[method]
            if a.from_cache:
                if method=='fusion_lr_full_shdb':
                    f=ROOT/'outputs/AFib/fusion_lr_full_shdb_20261118';au=json.loads((f/'publication_audit.json').read_text())
                    saved=pd.read_csv(checked(f/'predictions.csv',au['predictions_sha256']),dtype={'record':str})
                    g=frame[['record','block','label']].merge(saved,on=['record','block','label'],validate='one_to_one')
                    assert len(g)==len(frame);score=g.score.to_numpy();prediction=g.prediction.to_numpy()
                else:score=frame[method+'_probability'].to_numpy();prediction=frame[method+'_prediction'].to_numpy()
            elif method=='fusion_lr_full_shdb':
                f=ROOT/'outputs/AFib/shdb_afdb_fusion_full_20261118'
                au=json.loads((f/'audit.json').read_text());assert au['status']=='PASS' and not au['diagnostic_subset']
                features=pd.read_csv(checked(f/'predictions.csv',au['predictions_sha256']),dtype={'record':str},low_memory=False)
                features=features[features.dataset.eq('AFDB')]
                fusion_frame=frame[['record','block']].merge(features,on=['record','block'],validate='one_to_one');assert len(fusion_frame)==len(frame)
                loaded=module.load(out/'build')
                score,prediction=module.predict(loaded,fusion_frame)
            elif method in ('ecgfounder','convnext'):
                import wfdb
                from AFib.modules._device import FrozenDevice
                model=module.load(out/'build');device=FrozenDevice(ROOT,out/'build');score=[];prediction=[]
                for record,g in frame.groupby('record',sort=False):
                    if record not in corrected:
                        raw=wfdb.rdrecord(str(ROOT/m['data']/record),channels=[0]);corrected[record]=device.replay(raw.p_signal[:,0],raw.fs)['corrected_mv']
                    for block in g.block:
                        s,pr=module.predict(model,corrected[record][int(block)*3600:(int(block)+1)*3600],360);score.append(s);prediction.append(pr)
                    print(method,record,len(g),'windows',flush=True)
                score=np.asarray(score);prediction=np.asarray(prediction)
            else:score,prediction=module.predict(module.load(out/'build'),frame)
            if len(score)!=len(frame) or not np.isfinite(score).all():raise ValueError('Incomplete common AF cohort')
            result=frame[['record','block','label']].copy();result['method']=method;result['score']=score;result['prediction']=prediction
            result['population']=len(frame) if a.limit else population;rows.append(result)
            print(method,'complete',flush=True)
        predictions=pd.concat(rows,ignore_index=True);predictions.to_csv(out/'predictions.csv',index=False)
        summary=metrics.summarize(predictions);summary.to_csv(out/'metrics_raw.csv',index=False)
        figures=out/'figures'
        plot(summary,figures/'balanced_accuracy.png','AF: same AFDB common cohort',y='ba',ylabel='Balanced accuracy')
        plot(summary,figures/'auc.png','AF ROC AUC: same AFDB common cohort',y='auc',ylabel='AUC')
        if not a.report:summary.to_csv(out/'metrics.csv',index=False)
        if a.report:
            from AFib.utils._summary import refresh
            refresh(verification=('AFib',None,summary),output=out)
        save(out/'protocol.json',dict(seed=SEED,aspect='AFib',methods=a.methods,cache_replay=a.from_cache,report_reproduction=a.report,diagnostic_subset=bool(a.limit),train_fusion=False,
            inputs='RR heads: frozen native5-feature inputs; waveform models: fresh10s ECG; fusion: frozen13-feature synchronized branch inputs',
            no_AFDB_fit_or_tuning=True,source_sha256={str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__),Path(metrics.__file__),*[Path(methods[k].__file__) for k in a.methods]]}))
        save(out/'status.json',dict(state='complete',windows_per_method=len(frame),methods=len(a.methods),figures=str(figures.relative_to(ROOT))))
        print(summary.to_string(index=False))
    except BaseException as e:save(out/'failure.json',dict(state='failed',reason=str(e)));raise

if __name__=='__main__':main()
