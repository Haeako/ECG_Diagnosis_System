# Local datasets

This directory is intentionally not versioned. Extract the following PhysioNet
datasets locally and pass their paths to the experiment:

- QT Database (`qt-database-1.0.0`)
- MIT-BIH Noise Stress Test Database
  (`mit-bih-noise-stress-test-database-1.0.0`)

The legacy CleanGuard experiment accepts `pu1`, `pu0`, or `atr`. The first two
are automatic ecgpuwave annotations, **not manual reference labels**. Its
selection order is preserved for reproducibility, not endorsed as ground truth.

The EMG quality benchmark requires the official source-reference `.atr` files.
Records without them are excluded explicitly; `.man`/`.q1c` label selected
beats only and are not substitutes for continuous beat scoring.

Download just the required public data with SHA-256 verification:

```powershell
python experiments/download_ecg_quality_data.py
```

This creates `data/qtdb` (headers, signals, available `.atr` and `RECORDS`)
and `data/nstdb` (`ma.hea`, `ma.dat`), official checksum manifests and download
manifests. Existing files are reused only after checksum validation.

Sources: [QTDB](https://physionet.org/content/qtdb/1.0.0/) and
[NSTDB](https://physionet.org/content/nstdb/1.0.0/), ODC Attribution License v1.0.
Contains information from these databases made available under that license.
