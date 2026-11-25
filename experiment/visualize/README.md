# Visualization workspace

All local viewers, standalone plotting scripts, figures and HTML results live
under this directory. Dataset inputs live under [`datasets/`](../datasets/README.md).

```text
visualize/
  scripts/       standalone Python plotting tools, grouped by their original module
  viewers/       offline ECG CSV viewers
  results/       PNG, SVG, HTML and document previews, preserving original run paths
  diagrams/      editable architecture diagrams
```

Quick entry points:

- [EDB: noise plots, 22 conditions + BW/MA/EM trends](results/BPM/edb_holdout_matrix_20261111/index.html)
- [ESP candidates: EDB accuracy including completed WIM cells](results/BPM/esp_candidate_comparison/index.html)
- [ECG CSV viewer](viewers/ecg-csv-viewer.html)
- [ECG CSV preview viewer](viewers/ecg-csv-viewer-preview.html)
- [AFib explanation report](results/outputs/AFib/selected_af_xai/shdb002_feature_explanation_20261104_final/explanations.html)

Run Python scripts from the repository root with the project environment:

```powershell
.\.venv\Scripts\python.exe visualize/scripts/legacy_firmware_tools/visualize_ecg.py --help
.\.venv\Scripts\python.exe visualize/scripts/legacy_firmware_tools/qrs_detect_visualize.py --help
.\.venv\Scripts\python.exe visualize/scripts/experiments/af_detection/plot_int8_deployment.py --help
```

Legacy scripts under `scripts/legacy_python/` retain their original configurable
input paths and need those settings adjusted before use.

`ecg_benchmark.artifacts.visual_path()` routes repository-local generated figures
to `visualize/results/<original repository path>`. The original path below
`results/` avoids collisions between runs with identical filenames. Explicit
outputs outside the repository are honored. CSV, JSON, model weights and logs
stay in the corresponding module's run directory. AFib HTML reports embed their
images and remain usable offline after relocation.

`migration_manifest.json` records the previous and current asset paths. Old
reports' recorded source hashes and dataset provenance are retained; active
Markdown links and runner defaults use the new paths.
