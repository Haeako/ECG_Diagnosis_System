"""Replay original QTDB through the compiled production C detector at 360 Hz.

No added noise, no per-record tuning/polarity selection. Only header-labeled
MLII is used, at its actual channel index (not necessarily channel 0).
All MLII records with .atr are reported, including bad results. Full-record
scoring is exploratory until annotation coverage/source alignment are audited.
"""
from pathlib import Path
import argparse
import csv
import ctypes
import hashlib
import json
import subprocess
import numpy as np
import wfdb
from scipy.signal import resample_poly, butter, sosfilt, sosfilt_zi

ROOT = Path(__file__).resolve().parents[1]
BEATS = set('NLRBAaJSVrFejnE/Q?')

def scores(ref, est):
    # Chronological one-to-one maximum-cardinality matching, tolerance 50 ms.
    i = j = tp = 0
    while i < len(ref) and j < len(est):
        if est[j] < ref[i] - 50:
            j += 1
        elif est[j] > ref[i] + 50:
            i += 1
        else:
            tp += 1
            i += 1
            j += 1
    fp, fn = len(est)-tp, len(ref)-tp
    return metrics(tp, fp, fn)

def metrics(tp, fp, fn):
    return dict(TP=tp, FP=fp, FN=fn, Se=tp/(tp+fn) if tp+fn else 0,
                PPV=tp/(tp+fp) if tp+fp else 0,
                F1=2*tp/(2*tp+fp+fn) if 2*tp+fp+fn else 0,
                event_ACC=tp/(tp+fp+fn) if tp+fp+fn else 0)

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--output', default='outputs/firmware_qtdb_mlii')
    p.add_argument('--records', nargs='*')
    args = p.parse_args()
    out = ROOT / args.output
    out.mkdir(parents=True, exist_ok=True)
    firmware = ROOT/'firmware/esp32/main'
    sources = [ROOT/'experiments/firmware_qtdb_host.c', firmware/'ecg_signal.c', firmware/'components/filter/SBfilter.c']
    dll = out/'detector.dll'
    subprocess.run(['gcc', '-std=c99', '-O2', '-shared', '-I'+str(firmware), *map(str,sources), '-o', str(dll)], check=True)
    lib = ctypes.CDLL(str(dll))
    run = lib.run_detector
    run.argtypes = [np.ctypeslib.ndpointer(np.int32, flags='C_CONTIGUOUS'), ctypes.c_uint,
                   np.ctypeslib.ndpointer(np.uint32, flags='C_CONTIGUOUS')]
    run.restype = ctypes.c_uint
    rows = []
    excluded = []
    data_hashes = {}
    records = args.records or sorted(f.stem for f in (ROOT/'data/qtdb').glob('*.hea'))
    for name in records:
        path = str(ROOT/'data/qtdb'/name)
        header = wfdb.rdheader(path)
        channels = [i for i, lead in enumerate(header.sig_name) if lead.strip().upper() == 'MLII']
        if not channels:
            excluded.append(dict(record=name, leads=header.sig_name, reason='no_MLII'))
            continue
        if not Path(path+'.atr').exists():
            excluded.append(dict(record=name, leads=header.sig_name, reason='missing_atr'))
            continue
        if len(channels) != 1:
            raise ValueError(f'{name}: ambiguous MLII channels {channels}')
        channel = channels[0]
        for suffix in ('.hea', '.dat', '.atr'):
            source = Path(path+suffix)
            data_hashes[source.name] = hashlib.sha256(source.read_bytes()).hexdigest()
        record = wfdb.rdrecord(path)
        ann = wfdb.rdann(path, 'atr')
        ref = np.array([s*1000/record.fs for s,sym in zip(ann.sample,ann.symbol) if sym in BEATS])
        # Fixed conversion 200 ADC counts/mV (no record-specific amplitude normalization).
        signal = resample_poly(record.p_signal[:,channel], 36, 25)
        assert record.fs == 250
        # Model the specified analog AC coupling, initialized at the first
        # sample. QTDB headers may retain large acquisition DC offsets.
        # This is causal and identical for every record, not peak alignment.
        hp = butter(1, 0.5, btype='highpass', fs=360, output='sos')
        signal, _ = sosfilt(hp, signal, zi=sosfilt_zi(hp)*signal[0])
        unbounded = np.rint(signal*200+2048)
        adc = np.ascontiguousarray(np.clip(unbounded,0,4095),dtype=np.int32)
        peaks = np.empty(len(adc), dtype=np.uint32)
        n = run(adc, len(adc), peaks)
        est = peaks[:n].astype(float)
        row = dict(record=name, lead=record.sig_name[channel], channel=channel, seconds=len(signal)/360,
                   clipped_samples=int(np.count_nonzero((unbounded<0)|(unbounded>4095))),
                   **scores(ref,est))
        rows.append(row)
        np.savez_compressed(out/(name+'.npz'), reference_ms=ref, estimate_ms=est)
        print(name, 'Se %.4f PPV %.4f F1 %.4f'%(row['Se'],row['PPV'],row['F1']),flush=True)
    (out/'selection.json').write_text(json.dumps(dict(
        rule='MLII label only, case-insensitive; no aliases; .atr required',
        included=[dict(record=r['record'],lead=r['lead'],channel=r['channel']) for r in rows],
        excluded=excluded),indent=2),encoding='utf-8')
    if not rows:
        raise ValueError('No records with MLII and .atr; see selection.json')
    with (out/'per_record.csv').open('w',newline='',encoding='utf-8') as f:
        writer = csv.DictWriter(f,fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    summary = metrics(*(sum(r[k] for r in rows) for k in ['TP','FP','FN']))
    summary.update(records=len(rows), fs=360, tolerance_ms=50, lead='MLII',
        channel_policy='select actual MLII channel from header', excluded_records=len(excluded),
        adc_counts_per_mV=200, added_noise=False, full_record_coverage_audit_pending=True,
        data_sha256=data_hashes,
        afe_simulation='causal first-order 0.5 Hz AC coupling, initial steady state',
        source_sha256={str(s.relative_to(ROOT)):hashlib.sha256(s.read_bytes()).hexdigest()
                       for s in sources+[firmware/'ecg_signal.h',Path(__file__)]})
    (out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    print(json.dumps(summary,indent=2))

if __name__ == '__main__':
    main()
