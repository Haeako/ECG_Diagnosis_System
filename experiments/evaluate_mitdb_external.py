"""Common-protocol MIT-BIH benchmark of every available QRS detector."""
from __future__ import annotations
import argparse,csv,ctypes,hashlib,json,os,subprocess,time,tracemalloc
from pathlib import Path
os.environ.setdefault("MPLCONFIGDIR",str((Path("outputs")/"mplconfig").resolve()))
os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL","2")
import numpy as np
import wfdb
from scipy.signal import butter,sosfilt,sosfilt_zi
from benchmark_bpm_qrs import load_classic_suite,load_external_detectors
from ecg_benchmark.rpeaks import detect_r_peaks_adaptive,detect_r_peaks_adaptive_v2,detect_r_peaks_pan_tompkins,estimate_bpm,score_r_peaks

ROOT=Path(__file__).resolve().parents[1];BEATS=set("NLRBAaJSVrFejE/fQ?");TOLS=(25.,50.,100.,150.)

def firmware_detector(out):
    main=ROOT/"firmware/esp32/main";sources=[ROOT/"experiments/firmware_current_host.c",main/"task/Core1/ECGPipeline/ExFeature/ExtractFeature.c",main/"components/filter/SBfilter.c"]
    dll=out/"firmware_current_detector.dll";subprocess.run(["gcc","-std=c99","-O2","-shared",f"-I{main}",*map(str,sources),"-o",str(dll)],check=True)
    lib=ctypes.CDLL(str(dll));run=lib.run_detector;run.argtypes=[np.ctypeslib.ndpointer(np.int32,flags="C_CONTIGUOUS"),ctypes.c_uint,np.ctypeslib.ndpointer(np.uint32,flags="C_CONTIGUOUS")];run.restype=ctypes.c_uint
    def detect(x,fs):
        if not np.isclose(fs,360):raise ValueError("Firmware requires 360 Hz")
        hp=butter(1,.5,btype="highpass",fs=360,output="sos");y,_=sosfilt(hp,x,zi=sosfilt_zi(hp)*x[0]);adc=np.ascontiguousarray(np.clip(np.rint(y*200+2048),0,4095),dtype=np.int32);peaks=np.empty(len(adc),dtype=np.uint32);n=run(adc,len(adc),peaks)
        return np.rint(peaks[:n].astype(float)*fs/1000).astype(np.int64)
    return detect

def write(path,rows):
    with path.open("w",newline="",encoding="utf-8") as f:w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--mitdb-dir",type=Path,default=Path("external/pan-tompkins-plus-plus/data/MIT-BIH"));p.add_argument("--external-root",type=Path,default=Path("external"));p.add_argument("--classic-suite",type=Path,default=Path("external/benchmark-qrs-detectors"));p.add_argument("--output",type=Path,default=Path("outputs/mitdb_all_detectors"));p.add_argument("--windows",type=int,default=10);p.add_argument("--window-sec",type=float,default=60);p.add_argument("--repeats",type=int,default=3);p.add_argument("--detectors",nargs="+");a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    methods={"firmware_current":firmware_detector(a.output),"local_pan_tompkins":detect_r_peaks_pan_tompkins,"adaptive_pt_v1":detect_r_peaks_adaptive,"adaptive_pt_v2_frozen":detect_r_peaks_adaptive_v2};methods.update(load_external_detectors(a.external_root));methods.update(load_classic_suite(a.classic_suite))
    if a.detectors:
        missing=set(a.detectors)-set(methods)
        if missing:raise ValueError(f"Unknown detectors: {sorted(missing)}")
        methods={k:methods[k] for k in a.detectors}
    records=sorted(x.stem for x in a.mitdb_dir.glob("*.dat") if (a.mitdb_dir/f"{x.stem}.atr").exists());rows=[];memory={k:[] for k in methods}
    for ri,record in enumerate(records,1):
        x,meta=wfdb.rdsamp(str(a.mitdb_dir/record),channels=[0]);fs=float(meta["fs"]);x=x[:,0]-np.median(x[:,0]);ann=wfdb.rdann(str(a.mitdb_dir/record),"atr");refall=np.asarray([s for s,z in zip(ann.sample,ann.symbol) if z in BEATS],dtype=np.int64);size=int(a.window_sec*fs)
        for wi,start in enumerate(np.linspace(0,len(x)-size,a.windows,dtype=np.int64)):
            segment=x[start:start+size];ref=refall[(refall>=start)&(refall<start+size)]-start
            for name,detector in methods.items():
                detector(segment,fs);timings=[]
                for _ in range(a.repeats):t=time.perf_counter_ns();est=detector(segment,fs);timings.append((time.perf_counter_ns()-t)/1e6)
                if wi==0:
                    tracemalloc.start();detector(segment,fs);_,peak=tracemalloc.get_traced_memory();tracemalloc.stop();memory[name].append(peak/1024)
                bpm=abs(estimate_bpm(ref,fs)-estimate_bpm(est,fs))
                for tol in TOLS:
                    s=score_r_peaks(ref,est,fs,tol);rows.append({"record":record,"window":wi,"detector":name,"tolerance_ms":tol,"tp":int(s["RPeak_TP"]),"fp":int(s["RPeak_FP"]),"fn":int(s["RPeak_FN"]),"bpm_abs_error":bpm,"latency_ms":float(np.median(timings))})
        print(f"[{ri:02d}/{len(records)}] {record}",flush=True)
    summary=[]
    for name in methods:
        for tol in TOLS:
            g=[r for r in rows if r["detector"]==name and r["tolerance_ms"]==tol];tp=sum(r["tp"] for r in g);fp=sum(r["fp"] for r in g);fn=sum(r["fn"] for r in g);pr=tp/max(tp+fp,1);re=tp/max(tp+fn,1)
            summary.append({"detector":name,"tolerance_ms":tol,"records":len(records),"windows":len(g),"beats":tp+fn,"precision":pr,"recall":re,"f1":2*pr*re/max(pr+re,1e-12),"bpm_mae":float(np.nanmean([r["bpm_abs_error"] for r in g])),"latency_median_ms":float(np.median([r["latency_ms"] for r in g])),"latency_p95_ms":float(np.percentile([r["latency_ms"] for r in g],95)),"traced_python_peak_kib":float(max(memory[name]))})
    write(a.output/"per_window.csv",rows);write(a.output/"summary.csv",summary);(a.output/"summary.json").write_text(json.dumps(summary,indent=2),encoding="utf-8")
    manifest={"dataset":"MIT-BIH Arrhythmia Database bundled by Pan-Tompkins++ repo","records":records,"lead":0,"windows_per_record":a.windows,"window_sec":a.window_sec,"tolerances_ms":TOLS,"detectors":list(methods),"timing_repeats":a.repeats,"parameters_changed_after_run":False,"memory_warning":"tracemalloc excludes native allocations and is not ESP32 RAM","runner_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest()};(a.output/"manifest.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8");print(json.dumps({"records":len(records),"detectors":len(methods),"rows":len(rows)},indent=2))
if __name__=="__main__":main()
