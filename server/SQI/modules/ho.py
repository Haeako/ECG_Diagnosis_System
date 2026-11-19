"""Ho ICI C kernel with frozen UNSW and NeuroKit peak providers."""
METHOD='Ho'
CACHE='outputs/SQI/ho_host_20261114'
def load(output):
    import ctypes as c,importlib
    import numpy as np
    from server.SQI.modules._runtime import neurokit,DIR,library
    neurokit();ref=importlib.import_module('neurokit2.signal.signal_quality')
    lib=library(output,'ho',[DIR/'sqi_ho.c']);call=lib.sqi_ho_ici_quality
    u32=np.ctypeslib.ndpointer(np.uint32,flags='C_CONTIGUOUS')
    call.argtypes=[np.ctypeslib.ndpointer(np.float32,flags='C_CONTIGUOUS'),c.c_size_t,u32,c.c_size_t,u32,c.c_size_t,c.c_uint32,c.c_uint32,u32,c.c_size_t,u32,c.c_size_t,np.ctypeslib.ndpointer(np.uint8,flags='C_CONTIGUOUS')]
    def score(w):
        x=w.core(500);p=np.ascontiguousarray(ref._signal_cycles(x,'ecg','unsw',500),np.uint32);s=np.ascontiguousarray(ref._signal_cycles(x,'ecg','neurokit',500),np.uint32)
        quality=np.zeros(5000,np.uint8);ps=np.empty(64,np.uint32);ss=np.empty(64,np.uint32)
        ok=call(x,len(x),p,len(p),s,len(s),500,50,ps,64,ss,64,quality)
        return float(quality.mean()) if ok else np.nan
    return score
