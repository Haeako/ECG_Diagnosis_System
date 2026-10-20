"""Read-only semantic validation of completed checkpoints during the live run.

Missing methods remain missing; this never writes a completion marker.
"""
import json
import numpy as np
from sklearn.metrics import confusion_matrix,roc_auc_score
from run_butqdb_survey import OUT,read_frame
from evaluate_butqdb_survey import TASKS,method_specs

w=read_frame(OUT/'windows.csv');specs=method_specs()
for task in TASKS:
    completed=[];pending=[]
    for i,spec in enumerate(specs):
        path=OUT/task/f'{i:02d}_metrics.json'
        if not path.exists():pending.append(spec['method']);continue
        # Metric JSON is published last, after the NPZ and stratum outputs.
        r=json.loads(path.read_text(encoding='utf-8'))
        p=np.load(OUT/task/f'{i:02d}_predictions.npz',allow_pickle=False)
        assert r['task']==task and r['method']==spec['method']
        assert np.array_equal(p['window_id'],w.window_id.to_numpy(str))
        assert np.array_equal(p['target'],w[task].to_numpy(int))
        assert np.array_equal(p['fold'],w.fold.to_numpy())
        assert np.isfinite(p['score']).all() and np.isin(p['prediction'],[0,1]).all()
        tn,fp,fn,tp=confusion_matrix(p['target'],p['prediction'],labels=[0,1]).ravel()
        assert [tn,fp,fn,tp]==[r[k] for k in ['tn','fp','fn','tp']]
        assert abs(roc_auc_score(p['target'],p['score'])-r['auc'])<1e-12
        assert abs(.5*(tp/(tp+fn)+tn/(tn+fp))-r['balanced_accuracy'])<1e-12
        assert r['n']==len(w)
        for suffix in ['subjects','strata']:
            assert (OUT/task/f'{i:02d}_{suffix}.json').exists()
        completed.append(spec['method']);p.close()
    print(json.dumps(dict(task=task,verified_completed=len(completed),required=len(specs),
                          complete=len(completed)==len(specs),pending=pending),ensure_ascii=False),flush=True)
