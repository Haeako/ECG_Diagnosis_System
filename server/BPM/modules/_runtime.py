"""BPM data protocol, native bridges and lazy algorithm loading."""
import ctypes, hashlib, importlib, json, os, sys, re
from pathlib import Path
import numpy as np
from scipy import signal
from server.modules._shared import ROOT,sha,save,load_source,build
LEVELS=(-6,0,6,12,18,24)
CONDITIONS=['original']+[f'{k}_{v}' for k in ('bw','ma','em') for v in LEVELS]+[f'{k}_mixed' for k in ('bw','ma','em')]
BEATS=set('NLRBAaJSVrFejnE/fQ')
def edb_qtdb_sources(root):
    headers=list((Path(root)/'datasets/qtdb').glob('*.hea'))
    manifest=(Path(root)/'datasets/qtdb/RECORDS').read_text().split()
    if len(manifest)!=105 or {p.stem for p in headers}!=set(manifest):
        raise RuntimeError('STOP: complete 105-record QTDB headers required for source holdout')
    sources={}
    for path in headers:
        match=re.search(r'Produced by xform from record (e\d+),',path.read_text())
        if match:
            sources.setdefault(match[1],[]).append(path.stem)
    return sources

def holdout_cohort(root):
    all_records=(Path(root)/'datasets/edb-1.0.0/european-st-t-database-1.0.0/RECORDS').read_text().split()
    if len(all_records)!=90 or len(set(all_records))!=90:
        raise RuntimeError('STOP: EDB cohort incomplete')
    mapping=edb_qtdb_sources(root)
    included=[r for r in all_records if r not in mapping]
    excluded={r:mapping[r] for r in all_records if r in mapping}
    if len(included)!=57 or len(excluded)!=33:
        raise RuntimeError('STOP: source mapping changed; review cohort before inference')
    return included,excluded

def mix_burst(raw, fs, noise, nfs, snr, record):
    """Deterministic 10s bursts starting at 10s every 30s; local demeaned RMS SNR."""
    noise = signal.resample_poly(noise, int(fs), int(nfs))
    offset = int(hashlib.sha256(record.encode()).hexdigest()[:8], 16) % len(noise)
    noise = np.resize(np.roll(noise, -offset), len(raw))
    result = raw.copy()
    measured = []
    for start in range(int(10*fs), len(raw), int(30*fs)):
        stop = min(start+int(10*fs), len(raw))
        x = raw[start:stop] - raw[start:stop].mean()
        n = noise[start:stop] - noise[start:stop].mean()
        xp, npower = np.mean(x*x), np.mean(n*n)
        if xp <= 0 or npower <= 0:
            raise ValueError('Cannot define burst SNR on zero-power segment')
        scaled = n * np.sqrt(xp / npower / 10**(snr/10))
        result[start:stop] += scaled
        measured.append(float(10*np.log10(xp/np.mean(scaled*scaled))))
    return result, measured

def mixed_bursts(raw,fs,noise,nfs,record):
    """Each 10s noise burst contains all six SNRs in six contiguous pieces.

    Burst starts at10s and repeats every30s. Rotate level order by burst index
    to avoid always assigning a level to the same within-burst position.
    SNR is calibrated per piece before the shared AFE/ADC, using demeaned RMS.
    """
    noise=signal.resample_poly(noise,int(fs),int(nfs))
    offset=int(hashlib.sha256(record.encode()).hexdigest()[:8],16)%len(noise)
    noise=np.resize(np.roll(noise,-offset),len(raw))
    result=raw.copy()
    measured=[]
    for burst,start in enumerate(range(int(10*fs),len(raw),int(30*fs))):
        stop=min(start+int(10*fs),len(raw))
        edges=np.rint(np.linspace(start,stop,7)).astype(int)
        for piece,(a,b) in enumerate(zip(edges[:-1],edges[1:])):
            if b<=a:
                raise ValueError('Mixed-noise piece is empty')
            level=LEVELS[(piece+burst)%6]
            clean=raw[a:b]-raw[a:b].mean()
            artifact=noise[a:b]-noise[a:b].mean()
            power=float(np.mean(clean**2));n_power=float(np.mean(artifact**2))
            if power<=0 or n_power<=0:
                raise ValueError('Cannot define mixed SNR on zero-power piece')
            addition=artifact*np.sqrt(power/n_power/10**(level/10))
            achieved=float(10*np.log10(power/np.mean(addition**2)))
            if not np.isclose(achieved,level,atol=1e-8,rtol=0):
                raise ValueError('Mixed-noise calibration failed')
            result[a:b]+=addition
            measured.append(achieved)
    return result,measured

def adc_from_mv(x, fs):
    assert np.isfinite(x).all()
    x = signal.resample_poly(x, 360, int(fs))
    hp = signal.butter(1,.5,btype='highpass',fs=360,output='sos')
    x,_ = signal.sosfilt(hp,x,zi=signal.sosfilt_zi(hp)*x[0])
    x = np.rint(x*200+2048)
    clipped = int(np.sum((x<0)|(x>4095)))
    return np.ascontiguousarray(np.clip(x,0,4095),dtype=np.int32),clipped


def cache_cells(method,cache):
    import pandas as pd
    source=ROOT/cache/'verified_complete_cells.csv'
    manifest=json.loads((ROOT/'visualize/results/BPM/host_rmse_latency/manifest.json').read_text())
    digest=manifest['source_sha256'][str(source.relative_to(ROOT))]
    if sha(source)!=digest:raise ValueError('Changed complete-cell cache')
    f=pd.read_csv(source);f=f[f.method.eq(method)]
    if len(f)!=44 or not f.records.eq(57).all():raise ValueError('Incomplete BPM method cache: '+method)
    return f

def cache_c_cells(method):
    import pandas as pd
    folder=ROOT/'outputs/BPM/c_module_window_matrix_20261116'
    audit=json.loads((folder/'publication_audit.json').read_text())
    path=folder/'complete_cells.csv'
    if sha(path)!=audit['cells_sha256']:raise ValueError('Changed C accuracy cells')
    values=pd.read_csv(path);values=values[values.method.eq(method)]
    if len(values)!=22:raise ValueError('No complete C cohort for '+method)
    qrs=ROOT/'outputs/BPM/c_qrs_tolerance_20261118'
    qa=json.loads((qrs/'publication_audit.json').read_text())
    if sha(qrs/'summary.csv')!=qa['summary_sha256']:raise ValueError('Changed C QRS summary')
    peaks=pd.read_csv(qrs/'condition_metrics.csv');peaks=peaks[peaks.method.eq(method)]
    summary=pd.read_csv(qrs/'summary.csv');summary=summary[summary.method.eq(method)].set_index('tolerance_ms')
    for t,g in peaks.groupby('tolerance_ms'):
        np.testing.assert_allclose(g.F1.mean(),summary.loc[t,'F1'],rtol=1e-12)
    result=values.merge(peaks,on=['method','condition'],validate='one_to_many')
    assert len(result)==44 and result.records.eq(57).all()
    return result.rename(columns={'BPM_MAE':'bpm_mae','BPM_RMSE':'bpm_rmse','BPM_coverage':'bpm_coverage','F1':'f1'})

def c_detector(method,output):
    import ctypes as c
    from server.modules._shared import registry
    m=registry('BPM')[method]
    if not hasattr(m,'C_PORT'):raise ValueError('No C port for '+method)
    base,state,detect=m.C_PORT
    mods=ROOT/'firmware/BPM/modules';kernels=ROOT/'firmware/esp32/main/components/qrs_detector'
    source=next(p for p in (mods/(base+'.c'),kernels/(base+'.c')) if p.exists())
    lib=ctypes.CDLL(str(build(output,base,[ROOT/'server/BPM/native/qrs_module_sliding_host.c',mods/'qrs_module.c',source],[mods,kernels],
        ['-ffp-contract=off','-DECG_QRS_HEADER="'+base+'.h"','-DECG_QRS_STATE='+state,'-DECG_QRS_DETECT='+detect])))
    call=lib.qrs_module_sliding
    call.argtypes=[np.ctypeslib.ndpointer(np.float32,flags='C_CONTIGUOUS'),c.c_size_t,np.ctypeslib.ndpointer(np.float32,flags='C_CONTIGUOUS'),np.ctypeslib.ndpointer(np.uint16,flags='C_CONTIGUOUS'),np.ctypeslib.ndpointer(np.uint16,flags='C_CONTIGUOUS')]
    call.restype=c.c_int
    def run(x):
        x=np.ascontiguousarray(x,np.float32);n=(len(x)-3600)//1800+1
        if n<1 or len(x)%1800:raise ValueError('C replay needs 10s context / 5s hop at360Hz')
        bpm=np.empty(n,np.float32);peaks=np.zeros((n,128),np.uint16);counts=np.empty(n,np.uint16)
        if call(x,len(x),bpm,peaks,counts):raise ValueError('Native detector invariant failure')
        return bpm,peaks,counts
    return run
