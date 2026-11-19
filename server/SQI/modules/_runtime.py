"""BUTQDB input conversion and explicitly scoped shared native peak providers."""
import ctypes,importlib,json,sys
from pathlib import Path
import numpy as np
from scipy import signal
from server.modules._shared import ROOT,build,sha
VENDOR=Path('D:/DOC/ECG_PAPER/temp/benchmark_results/vendor/neurokit2_source')
DIR=ROOT/'firmware/esp32/main/components/sqi_gate'
MAIN=ROOT/'firmware/esp32/main'

def neurokit():
    sys.path.insert(0,str(VENDOR));sys.dont_write_bytecode=True
    import neurokit2 as nk
    if nk.__version__!='0.2.13':raise ValueError('Frozen NeuroKit0.2.13 required')
    return nk

def library(output,name,sources,flags=()):
    return ctypes.CDLL(str(build(output,name,sources,[MAIN],flags)))

def cached(module):
    import pandas as pd
    folder=ROOT/module.CACHE
    audit=json.loads((folder/'audit.json').read_text())
    key='predictions_sha256' if 'predictions_sha256' in audit else 'prediction_sha256'
    assert audit['status']=='PASS' and sha(folder/'predictions.csv')==audit[key]
    f=pd.read_csv(folder/'predictions.csv',low_memory=False)
    if 'method' in f:f=f[f.method.eq(module.CACHE_METHOD)]
    if len(f)!=35773:raise ValueError('Incomplete SQI cache')
    return f[['window_id','score']].assign(method=module.METHOD)

class Window:
    def __init__(self,raw,item,events=None):
        self.raw=raw;self.start=int(item['start_sample']);self.end=int(item['end_sample']);self.events=events;self.values={}
    def context(self,rate,padding=1000):
        a=max(0,self.start-padding);b=min(len(self.raw),self.end+padding)
        x=np.asarray(self.raw[a:b,0],float)/1000
        if not np.isfinite(x).all():raise ValueError('Nonfinite ECG input')
        y=signal.resample_poly(x,rate,1000,padtype='line')
        return y,(self.start-a)*rate//1000,a
    def core(self,rate,dtype=np.float32):
        x,offset,_=self.context(rate)
        return np.ascontiguousarray(x[offset:offset+rate*10],dtype=dtype)
    def primary(self):
        if self.events is None:raise ValueError('Audited native primary-peak cache required')
        lo,hi=np.searchsorted(self.events[:,0],[self.start,self.end]);p=self.events[lo:hi,1]
        return np.ascontiguousarray(p[(p>=self.start)&(p<self.end)]-self.start,np.uint32)
    def clean(self):
        if 'clean' not in self.values:
            nk=neurokit();x,offset,_=self.context(500)
            y=nk.ecg_clean(x,sampling_rate=500,method='neurokit',powerline=50)
            _,info=nk.ecg_peaks(y,sampling_rate=500,method='neurokit',correct_artifacts=False)
            p=np.asarray(info['ECG_R_Peaks']);p=p[(p>=offset)&(p<offset+5000)]-offset
            self.values['clean']=(np.asarray(y[offset:offset+5000]),p)
        return self.values['clean']

def events(record):
    p=ROOT/'outputs/SQI/continuous_profile_20261110/replay/records'/record/'native_events.npz'
    a=json.loads(p.with_name('audit.json').read_text())
    if sha(p)!=a['output_hashes']['native_events.npz']:raise ValueError('Changed native peak cache')
    with np.load(p) as d:return d['peaks'].astype(np.int64)
