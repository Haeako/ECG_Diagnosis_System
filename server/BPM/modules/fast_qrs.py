"""FastQRS_0.3.0: isolated implementation adapter."""
METHOD='FastQRS_0.3.0'
CACHE='outputs/BPM/edb_holdout_matrix_20261111'
C_PORT=('fast_qrs_port', 'fast_qrs_workspace_t', 'fast_qrs_detect_360')
from server.BPM.modules._runtime import ROOT,load_source
def load(output=None):
    import sys
    import scipy.signal as signal
    if not hasattr(signal,'flattop'):signal.flattop=signal.windows.flattop
    sys.path.insert(0,str(ROOT/'external/fast-qrs-detector/src'))
    from fast_qrs_detector import qrs_detector
    return lambda x:qrs_detector(x,360)
