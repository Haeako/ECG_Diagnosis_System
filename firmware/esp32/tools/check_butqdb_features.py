"""Pre-evaluation extraction audit (does not claim survey completion)."""
import json
import inspect
import numpy as np
from run_butqdb_survey import OUT,FW,ARCHIVE,read_frame,sha,save_json,extract

w=read_frame(OUT/'windows.csv');f=read_frame(OUT/'features.csv');s=read_frame(OUT/'smoke_features.csv')
assert len(w)==len(f)==35773 and f.window_id.is_unique
assert set(f.window_id)==set(w.window_id)
required=['bsqi_f1','orphanidou_score','orphanidou_accept','kramer_score','kramer_accept','zhao_score','zhao_accept','ho_score']
assert np.isfinite(f[required].to_numpy()).all()
assert ((f[required]>=0)&(f[required]<=1)).all().all()
for col in ['orphanidou_accept','kramer_accept','zhao_accept']:assert f[col].isin([0,1]).all()
assert f.zhao_label.isin(['Unacceptable','Barely acceptable','Excellent']).all()
assert f.zhao_accept.eq(f.zhao_label.ne('Unacceptable').astype(int)).all()
numeric=[c for c in s.select_dtypes('number') if not c.endswith('_seconds')]
assert np.allclose(s.set_index('window_id')[numeric],f.set_index('window_id').loc[s.window_id,numeric],rtol=1e-12,atol=1e-12,equal_nan=True)
paths=[FW/'tools/run_butqdb_survey.py',FW/'tools/run_cinc2011_verified.py',OUT/'protocol.json',OUT/'windows.csv',OUT/'features.csv',
       ARCHIVE/'clifford2012_reproduction/run_reproduction.py',ARCHIVE/'clifford2012_reproduction/compare_one_lead_categories.py',
       ARCHIVE/'clifford2012_reproduction/external/eplimited/eplimited.dll',
       ARCHIVE/'vendor/neurokit2_source/neurokit2/ecg/ecg_quality.py',
       ARCHIVE/'vendor/neurokit2_source/neurokit2/ecg/ecg_findpeaks.py',
       ARCHIVE/'vendor/neurokit2_source/neurokit2/signal/signal_quality.py']
result=dict(extraction_verified=True,survey_complete=False,windows=len(f),smoke_full_parity=len(s),
    warnings=f.warnings.value_counts().to_dict(),undefined_features={k:int(v) for k,v in f.isna().sum().items() if v>0},
    fixed_scores_all_finite=True,hashes={str(p):sha(p) for p in paths})
save_json(OUT/'feature_audit.json',result)
print(json.dumps({k:v for k,v in result.items() if k!='hashes'},indent=2))
