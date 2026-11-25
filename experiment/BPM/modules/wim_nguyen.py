"""WIM_nguyen: isolated implementation adapter."""
METHOD='WIM_nguyen'
CACHE='outputs/BPM/edb_wim_matrix_20261113/full'
C_PORT=('wim_nguyen', 'wim_nguyen_workspace_t', 'wim_nguyen_detect_360')
from BPM.modules._runtime import ROOT,load_source
def load(output=None):
    import sys
    sys.path.insert(0,str(ROOT/'external/rpeak_benchmarking'))
    module=load_source(ROOT/'external/rpeak_benchmarking/algorithms/nguyen.py','_bpm_wim_nguyen')
    fn=getattr(module,'nguyen')
    def detect(x):
        import numpy as np
        y=np.asarray(fn(data=x,samplerate=360))
        if y.ndim!=1 or not np.isfinite(y).all():raise ValueError('Invalid WIM output')
        if len(y)==len(x) and np.isin(y,[0,1]).all():return np.flatnonzero(y)
        if not np.equal(y,np.floor(y)).all() or np.any(y<0) or np.any(y>=len(x)):raise ValueError('Invalid peak index')
        return y.astype(np.int64)
    return detect
