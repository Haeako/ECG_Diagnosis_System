"""Native full-waveform Kramer at250Hz, reset per10s window."""
METHOD='Kramer C'
CACHE='outputs/SQI/kramer_host_20261114'
def load(output):
    import ctypes as c
    import numpy as np
    from server.SQI.modules._runtime import library,DIR,ROOT
    lib=library(output,'kramer',[ROOT/'server/SQI/native/kramer_host.c',DIR/'sqi_kramer.c',DIR/'sqi_kramer_full.c'])
    lib.kramer_window.argtypes=[np.ctypeslib.ndpointer(np.float32,flags='C_CONTIGUOUS'),c.c_size_t,np.ctypeslib.ndpointer(np.float64,flags='C_CONTIGUOUS')]
    def score(w):
        value=np.empty(7,np.float64);x=w.core(250)
        if lib.kramer_window(x,len(x),value):raise ValueError('Native Kramer invariant')
        return float(value[0])
    return score
