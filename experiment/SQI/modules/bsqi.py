"""Native bSQI with cached continuous primary and native XQRS secondary."""
import ctypes as c
METHOD='bSQI C'
CACHE='outputs/SQI/bsqi_host_20261114'
class State(c.Structure):
    _fields_=[('primary',c.c_uint32*32),('secondary',c.c_uint32*32),
              ('primary_count',c.c_uint8),('secondary_count',c.c_uint8),('overflow',c.c_bool)]

def load(output):
    import numpy as np
    from SQI.modules._runtime import DIR,library
    lib=library(output,'bsqi',[DIR/'sqi_bsqi.c',DIR/'sqi_xqrs.c'])
    lib.sqi_xqrs_static_bytes.restype=c.c_size_t
    workspace=c.create_string_buffer(lib.sqi_xqrs_static_bytes())
    lib.sqi_xqrs_detect_250.argtypes=[np.ctypeslib.ndpointer(np.float32,flags='C_CONTIGUOUS'),c.c_size_t,np.ctypeslib.ndpointer(np.uint16,flags='C_CONTIGUOUS'),c.c_size_t,c.c_void_p]
    lib.sqi_xqrs_detect_250.restype=c.c_size_t
    lib.sqi_bsqi_reset.argtypes=[c.POINTER(State)]
    for name in ('primary','secondary'):
        getattr(lib,'sqi_bsqi_push_'+name).argtypes=[c.POINTER(State),c.c_uint32]
    lib.sqi_bsqi_finalize.argtypes=[c.POINTER(State),c.c_uint32];lib.sqi_bsqi_finalize.restype=c.c_float
    def score(w):
        x,_,a=w.context(250,2000);x=np.ascontiguousarray(x,np.float32);p=w.primary();peaks=np.empty(512,np.uint16)
        count=lib.sqi_xqrs_detect_250(x,len(x),peaks,len(peaks),workspace)
        s=peaks[:count].astype(np.int64)*4+a;s=s[(s>=w.start)&(s<w.end)]-w.start
        state=State();lib.sqi_bsqi_reset(c.byref(state))
        for name,values in [('primary',p),('secondary',s)]:
            for value in values:getattr(lib,'sqi_bsqi_push_'+name)(c.byref(state),int(value))
        return float(lib.sqi_bsqi_finalize(c.byref(state),50))
    return score
