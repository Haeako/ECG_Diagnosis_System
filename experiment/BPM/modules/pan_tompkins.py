"""PanTompkins_pyecgdetectors: isolated implementation adapter."""
METHOD='PanTompkins_pyecgdetectors'
CACHE='outputs/BPM/edb_holdout_matrix_20261111'
C_PORT=('pan_pyecg', 'pan_pyecg_workspace_t', 'pan_pyecg_detect_360')
from BPM.modules._runtime import ROOT,load_source
def load(output=None):
    from ecgdetectors import Detectors
    return Detectors(360).pan_tompkins_detector
