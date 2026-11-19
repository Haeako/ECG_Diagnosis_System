"""Elgendi_pyecgdetectors: isolated implementation adapter."""
METHOD='Elgendi_pyecgdetectors'
CACHE='outputs/BPM/edb_holdout_matrix_20261111'
C_PORT=('elgendi_pyecg', 'elgendi_pyecg_workspace_t', 'elgendi_pyecg_detect_360')
from server.BPM.modules._runtime import ROOT,load_source
def load(output=None):
    from ecgdetectors import Detectors
    return Detectors(360).two_average_detector
