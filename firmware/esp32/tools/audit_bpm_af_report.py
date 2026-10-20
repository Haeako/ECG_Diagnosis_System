"""Independent post-run checks; failures abort rather than publishing a pass."""
import hashlib
import json
from pathlib import Path
import numpy as np
import pandas as pd
from sklearn.metrics import confusion_matrix, roc_auc_score

BASE=Path(__file__).resolve().parents[1]
OUT=BASE/'benchmark_runs/non_sqi_20261019'
def read(path):return pd.read_csv(path,float_precision='round_trip',dtype={'record_id':str,'record':str})
metrics=read(OUT/'af_metrics.csv')
predictions=read(OUT/'af_predictions.csv')
checked=0
for row in metrics.itertuples():
    if row.dataset.startswith('runtime_'):
        frame=read(OUT/f'af_{row.dataset}.csv')
        y=frame.label.to_numpy();p=frame[row.model.removeprefix('INT8_C_')].to_numpy();s=frame.probability.to_numpy()
    else:
        frame=predictions[(predictions.dataset==row.dataset)&(predictions.model==row.model)]
        assert not frame.duplicated(['dataset','model','row_id']).any()
        y=frame.label.to_numpy();p=frame.prediction.to_numpy();s=frame.probability.to_numpy()
    tn,fp,fn,tp=confusion_matrix(y,p,labels=[0,1]).ravel()
    assert (tn,fp,fn,tp)==(row.tn,row.fp,row.fn,row.tp)
    assert len(y)==row.windows
    assert abs((tn+tp)/len(y)-row.accuracy)<1e-12
    assert abs(.5*(tp/(tp+fn)+tn/(tn+fp))-row.balanced_accuracy)<1e-12
    assert abs(roc_auc_score(y,s)-row.roc_auc)<1e-12
    checked+=1

bpm=json.loads((OUT/'bpm_current_metrics.json').read_text())
window=read(OUT/'bpm_display_windows.csv');rr=read(OUT/'rr_adjacent_pairs.csv')
records=read(OUT/'bpm_qrs_per_record.csv')
assert records[['TP','FP','FN']].sum().tolist()==[bpm[k] for k in ['TP','FP','FN']]
assert len(window)==bpm['display_bpm']['total']
assert abs(window.error.abs().mean()-bpm['display_bpm']['mae'])<1e-12
assert abs((window.error.abs()<=5).mean()-bpm['display_bpm']['within_5_all'])<1e-12
assert abs(rr.error_ms.abs().mean()-bpm['adjacent_rr']['mae'])<1e-12
assert len(rr)==bpm['adjacent_rr']['valid']

manifest=json.loads((OUT/'replay_manifest.json').read_text())
for path,digest in {**manifest['source_sha256'],**manifest['data_sha256']}.items():
    assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest,path
model_cost=read(OUT/'af_host_cost.csv')
for row in model_cost.itertuples():
    path=BASE.parents[1]/f'outputs/af_detection/final_benchmark/models/{row.model}.pkl'
    if row.model.endswith('INT8_C'):path=BASE/'main/components/af_model/af_model_int8.c'
    assert hashlib.sha256(path.read_bytes()).hexdigest()==row.sha256
audit=json.loads((OUT/'af_frozen_audit.json').read_text())
assert audit['all_original_metrics_reproduced']
assert checked==62
assert len(metrics[metrics.dataset=='current_clean'])==9
assert len(manifest['test_records'])==6

# Hash both result files and exact runner/test sources at publication.
files=[p for p in OUT.iterdir() if p.is_file() and p.name!='final_audit.json']
files += [BASE/'tools'/name for name in ['complete_bpm_af_report.py','non_sqi_replay_host.c',
                                       'test_complete_bpm_af_report.py','audit_bpm_af_report.py']]
result=dict(passed=True,af_rows_recomputed=checked,annotation_models_exact=8,
            af_prediction_rows=len(predictions),bpm_display_points=len(window),rr_pairs=len(rr),
            no_sqi_evaluation=True,no_model_refit=True,source_data_model_hashes_match=True,
            artifact_sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files})
(OUT/'final_audit.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='artifact_sha256'},indent=2))
