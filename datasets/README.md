# Local datasets

This directory is intentionally not versioned. Store ECG datasets here and
pass these paths to the experiment runners:

- `qtdb/`: QT Database records and source-reference annotations.
- `nstdb/`: MIT-BIH Noise Stress Test Database records.
- `afdb/`: AFDB waveform and rhythm/beat annotations.
- `edb-1.0.0/european-st-t-database-1.0.0/`: verified European ST-T Database, 90 complete records. Use this nested directory; partial files in its parent are not benchmark input.
- `esp32_archive/`: archived waveform collections (BUT PPG, BUT-QDB snapshot, CinC2011 SQI, LTAFDB, MITDB, SHDB-AF, VitalDB) and one incomplete `.crdownload` file.
- `butqdb/`: BUT-QDB clone/data module, preserved as an isolated snapshot.

The legacy CleanGuard experiment accepts `pu1`, `pu0`, or `atr`. The first two
are automatic ecgpuwave annotations, **not manual reference labels**. Its
selection order is preserved for reproducibility, not endorsed as ground truth.

The EMG quality benchmark requires the official source-reference `.atr` files.
Records without them are excluded explicitly; `.man`/`.q1c` label selected
beats only and are not substitutes for continuous beat scoring.

Download the required public QTDB/NSTDB files with SHA-256 verification:

```powershell
python experiment/src/experiments/download_ecg_quality_data.py
```

This creates `datasets/qtdb` (headers, signals, available `.atr` and
`RECORDS`) and `datasets/nstdb` (`ma.hea`, `ma.dat`), official checksum
manifests and download manifests. Existing files are reused only after checksum
validation.

Sources: [QTDB](https://physionet.org/content/qtdb/1.0.0/) and
[NSTDB](https://physionet.org/content/nstdb/1.0.0/), ODC Attribution License v1.0.
Contains information from these databases made available under that license.

Additional workspace BUT-QDB snapshot: `butqdb_workspace/`. The snapshots remain separate to preserve provenance.

`vendor/` provides junctions to datasets bundled with external repositories; their source trees remain untouched.
Legacy `experiment/data` and root `butqdb` paths are compatibility junctions, not duplicate data.
