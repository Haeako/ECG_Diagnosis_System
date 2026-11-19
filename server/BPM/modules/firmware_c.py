"""firmware_C: isolated implementation adapter."""
METHOD='firmware_C'
CACHE='outputs/BPM/edb_holdout_matrix_20261111'
from server.BPM.modules._runtime import ROOT,load_source

import ctypes
import numpy as np
from server.modules._shared import build
def load(output):
    main=ROOT/'firmware/esp32/main'
    lib=ctypes.CDLL(str(build(output,'bpm',[ROOT/'server/BPM/native/bpm_host.c',main/'components/bpm/ExtractFeature.c',main/'components/filter/SBfilter.c'],[main])))
    u32=np.ctypeslib.ndpointer(np.uint32,flags='C_CONTIGUOUS')
    lib.bpm_replay.argtypes=[np.ctypeslib.ndpointer(np.int32,flags='C_CONTIGUOUS'),ctypes.c_uint32,u32,ctypes.c_uint32,u32,ctypes.c_uint32,u32]
    def run(adc):
        peaks=np.empty(len(adc)//50+100,np.uint32);bpm=np.empty((len(adc)//1800+10,2),np.uint32);counts=np.zeros(2,np.uint32)
        code=lib.bpm_replay(adc,len(adc),peaks,len(peaks),bpm,len(bpm),counts)
        if code:raise ValueError('Native BPM capacity error')
        return peaks[:counts[0]].astype(float),bpm[:counts[1],0].astype(float),bpm[:counts[1],1].astype(float)
    return run
