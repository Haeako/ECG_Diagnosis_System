"""Tune Adaptive PT v2 exclusively on the pre-declared 12-record development split."""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import sys
import time
from pathlib import Path

import numpy as np
import wfdb
from scipy import signal

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "experiments"))
from noise_coverage_3stage import (  # noqa: E402
    DEVELOPMENT_RECORDS, add_at_snr, annotation_peaks, normalized_noise,
)
from ecg_benchmark.rpeaks import detect_r_peaks_adaptive, score_r_peaks  # noqa: E402


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qtdb-dir", type=Path, default=Path("data/qtdb")); parser.add_argument("--nstdb-dir", type=Path, default=Path("data/nstdb"))
    parser.add_argument("--output", type=Path, default=Path("outputs/adaptive_pt_v2_dev")); parser.add_argument("--windows-per-record", type=int, default=3)
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    noise_bank = {}
    for name in ("bw", "ma", "em"):
        samples, meta = wfdb.rdsamp(str(args.nstdb_dir/name)); noise_bank[name] = (samples[:, 0], float(meta["fs"]))
    cases = []
    for record in sorted(DEVELOPMENT_RECORDS):
        samples, meta = wfdb.rdsamp(str(args.qtdb_dir/record)); fs = float(meta["fs"]); raw = samples[:, 0]-np.median(samples[:, 0]); size=int(30*fs)
        bank = {name: values if np.isclose(source_fs, fs) else signal.resample_poly(values, int(fs), int(source_fs)) for name,(values,source_fs) in noise_bank.items()}
        for window,start in enumerate(np.linspace(0,len(raw)-size,args.windows_per_record,dtype=np.int64)):
            clean=raw[start:start+size]; ref=annotation_peaks(args.qtdb_dir/record,int(start),int(start+size))
            if len(ref)<2: continue
            cases.append(("clean",clean,ref,fs))
            for name in ("bw","ma","em"):
                noise=normalized_noise(bank[name],len(clean),("v2-dev",record,window,name))
                for snr in (0,-6): cases.append((f"{name}_{snr}dB",add_at_snr(clean,noise,snr),ref,fs))
    rows=[]
    for refractory,integration,threshold in itertools.product((220.,240.,260.),(90.,120.,150.),(.18,.23,.28,.33,.38)):
        totals={condition:[0,0,0] for condition,*_ in cases}; latencies=[]
        for condition,x,ref,fs in cases:
            before=time.perf_counter_ns(); est=detect_r_peaks_adaptive(x,fs,refractory,integration,threshold); latencies.append((time.perf_counter_ns()-before)/1e6)
            score=score_r_peaks(ref,est,fs,100.0); totals[condition][0]+=int(score["RPeak_TP"]);totals[condition][1]+=int(score["RPeak_FP"]);totals[condition][2]+=int(score["RPeak_FN"])
        condition_f1={}
        for condition,(tp,fp,fn) in totals.items():
            p=tp/max(tp+fp,1);r=tp/max(tp+fn,1);condition_f1[condition]=2*p*r/max(p+r,1e-12)
        macro=float(np.mean(list(condition_f1.values()))); worst=min(condition_f1.values())
        rows.append({"refractory_ms":refractory,"integration_ms":integration,"threshold_scale":threshold,
                     "objective":.6*macro+.4*worst,"macro_f1_100ms":macro,"worst_condition_f1_100ms":worst,
                     "latency_median_ms":float(np.median(latencies)),**{f"f1_{k}":v for k,v in sorted(condition_f1.items())}})
    rows.sort(key=lambda row:(-float(row["objective"]),float(row["latency_median_ms"])))
    write_csv(args.output/"grid_results.csv",rows)
    best=rows[0]; frozen={"version":"adaptive_pt_v2","development_records":sorted(DEVELOPMENT_RECORDS),"test_records_used":[],
                         "selection_rule":"maximize 0.6*macro_F1 + 0.4*worst-condition_F1 at 100 ms; latency tie-break",
                         "conditions":["clean","bw 0/-6 dB","ma 0/-6 dB","em 0/-6 dB"],
                         "parameters":{key:best[key] for key in ("refractory_ms","integration_ms","threshold_scale")},
                         "development_metrics":best}
    (args.output/"frozen_config.json").write_text(json.dumps(frozen,indent=2),encoding="utf-8")
    print(json.dumps(frozen,indent=2))


if __name__=="__main__": main()
