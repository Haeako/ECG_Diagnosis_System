"""GQRS_WFDB: isolated implementation adapter."""
METHOD='GQRS_WFDB'
CACHE='outputs/BPM/edb_holdout_matrix_20261111'
from BPM.modules._runtime import ROOT,load_source
def load(output=None):
    from wfdb import processing
    return lambda x:processing.gqrs_detect(sig=x,fs=360)
