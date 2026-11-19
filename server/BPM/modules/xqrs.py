"""XQRS_WFDB: isolated implementation adapter."""
METHOD='XQRS_WFDB'
CACHE='outputs/BPM/edb_holdout_matrix_20261111'
from server.BPM.modules._runtime import ROOT,load_source
def load(output=None):
    from wfdb import processing
    return lambda x:processing.xqrs_detect(x,fs=360,verbose=False)
