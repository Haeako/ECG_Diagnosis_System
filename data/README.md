# Local datasets

This directory is intentionally not versioned. Extract the following PhysioNet
datasets locally and pass their paths to the experiment:

- QT Database (`qt-database-1.0.0`)
- MIT-BIH Noise Stress Test Database
  (`mit-bih-noise-stress-test-database-1.0.0`)

The final experiment requires QTDB waveform files and `pu1`, `pu0`, or `atr`
annotation files. It fails instead of silently substituting detector-generated
pseudo-labels when annotations are unavailable.
