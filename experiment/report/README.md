# Research reports

- Main QRS/RR/BPM report: `qrs_rr_bpm/REPORT.md`
- Workspace benchmark summary: `REPORT_METRICS.md`
- Vietnamese project progress summary: `README_TONG_HOP_TIEN_DO_ECG.md`
- Vietnamese thesis report (updated 31/10/2026): `BAO_CAO_KHOA_LUAN_ECG.tex`
- Compiled preview: `BAO_CAO_KHOA_LUAN_ECG.pdf`
- Historical Vietnamese progress report: `TONG_BAO_CAO_TIEN_DO_ECG.tex`
- Summary tables: `csv/`
- Per-run reports, raw artifacts, protocols, and audits: `benchmark_runs/`
- Other workspace reports and legacy summaries: this directory
- Run outputs and machine-readable scorecards: `outputs/`
- Host benchmark artifacts and audits: `host_bench/tomas_construe/artifacts/`

The CSV files use UTF-8, comma delimiters, and periods for decimal marks. In
Excel with Vietnamese locale, use **Data > From Text/CSV** and choose comma as
the delimiter.

Compile the current thesis report from this directory using XeLaTeX (TeX Live
or MiKTeX with Vietnamese support):

```powershell
xelatex -interaction=nonstopmode -halt-on-error BAO_CAO_KHOA_LUAN_ECG.tex
xelatex -interaction=nonstopmode -halt-on-error BAO_CAO_KHOA_LUAN_ECG.tex
```

Run the compiler twice to update the table of contents and references. The
source contains its diagram and bibliography, so no external image or `.bib`
file is required. Tectonic can also compile it; see
`../src/tools/latex_runtime/README.md` for the portable local setup.

The report separates implemented components, measured results, unsuccessful
experiments and proposed extensions. The canonical QRS/RR/BPM benchmark remains
`qrs_rr_bpm/REPORT.md`.
