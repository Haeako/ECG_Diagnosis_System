"""Audit selection, checksums and temporal extent without editing annotations."""
from collections import Counter
from pathlib import Path
import csv
import hashlib
import json
import numpy as np
import wfdb

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT/'data/qtdb'
OUT = ROOT/'outputs/firmware_qtdb_mlii_verified'
BEATS = set('NLRBAaJSVrFejnE/Q?')
manifest = {}
for line in (DATA/'SHA256SUMS.txt').read_text().splitlines():
    digest, name = line.split(maxsplit=1)
    manifest[name.lstrip('*').removeprefix('./')] = digest
selection = json.loads((OUT/'selection.json').read_text())
summary = json.loads((OUT/'summary.json').read_text())
rows = []
total = 0
for item in selection['included']:
    name = item['record']
    for suffix in ('.hea','.dat','.atr'):
        filename = name+suffix
        digest = hashlib.sha256((DATA/filename).read_bytes()).hexdigest()
        assert digest == manifest[filename] == summary['data_sha256'][filename], filename
    header = wfdb.rdheader(str(DATA/name))
    assert header.sig_name[item['channel']].strip().upper() == 'MLII'
    ann = wfdb.rdann(str(DATA/name),'atr')
    beats = np.array([sample for sample,symbol in zip(ann.sample,ann.symbol) if symbol in BEATS])
    assert len(beats) and np.all(np.diff(beats)>0)
    assert beats[0]>=0 and beats[-1]<header.sig_len
    total += len(beats)
    gaps = np.diff(beats)/header.fs
    rows.append(dict(record=name, channel=item['channel'], reference_beats=len(beats),
        first_beat_s=beats[0]/header.fs, tail_after_last_beat_s=(header.sig_len-1-beats[-1])/header.fs,
        max_RR_s=float(gaps.max()), gaps_over_3s=int((gaps>3).sum()),
        empty_10s_windows=int(sum(not np.any((beats>=start)&(beats<start+10*header.fs))
                                  for start in range(0,header.sig_len,int(10*header.fs)))),
        beat_symbols=json.dumps(dict(Counter(s for s in ann.symbol if s in BEATS))),
        other_annotations=json.dumps(dict(Counter(s for s in ann.symbol if s not in BEATS))),
        checksums_match=True))
assert total == summary['TP']+summary['FN']
with (OUT/'annotation_coverage.csv').open('w',newline='',encoding='utf-8') as f:
    writer=csv.DictWriter(f,fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
report = dict(records=len(rows), checked_files=3*len(rows), reference_beats=total,
              empty_10s_windows=sum(r['empty_10s_windows'] for r in rows),
              gaps_over_3s=sum(r['gaps_over_3s'] for r in rows),
              max_leading_s=max(r['first_beat_s'] for r in rows),
              max_trailing_s=max(r['tail_after_last_beat_s'] for r in rows),
              checksum_manifest_sha256=hashlib.sha256((DATA/'SHA256SUMS.txt').read_bytes()).hexdigest(),
              limitation='Structural coverage audit only; does not independently adjudicate every beat or prove clinical annotation correctness.')
(OUT/'annotation_audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
