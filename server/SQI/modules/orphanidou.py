"""Orphanidou external C, C primary peaks and raw360Hz input."""
METHOD='Orphanidou external C'
CACHE='outputs/SQI/shared_peaks_host_20261114'
CACHE_METHOD=METHOD
def load(output):
    import ctypes as c
    import numpy as np
    from server.SQI.modules._runtime import ROOT,DIR,library
    names=['sqi_zhao','sqi_runtime','sqi_orphanidou','sqi_template_matching']
    lib=library(output,'orphanidou',[ROOT/'server/SQI/native/shared_peak_host.c',*[DIR/(n+'.c') for n in names]],['-DSQI_BENCHMARK_ENABLE=1'])
    lib.shared_peak_window.argtypes=[np.ctypeslib.ndpointer(np.float32,flags='C_CONTIGUOUS'),np.ctypeslib.ndpointer(np.int16,flags='C_CONTIGUOUS'),c.c_size_t,np.ctypeslib.ndpointer(np.uint32,flags='C_CONTIGUOUS'),c.c_size_t,np.ctypeslib.ndpointer(np.float64,flags='C_CONTIGUOUS')]
    def score(w):
        x=w.core(360);adc=np.ascontiguousarray(np.clip(np.rint(x.astype(float)*200),-32768,32767),np.int16);p=w.primary();value=np.empty(11,np.float64)
        if lib.shared_peak_window(x,adc,len(x),p,len(p),value):raise ValueError('Native shared SQI invariant')
        return float(value[0])
    return score
