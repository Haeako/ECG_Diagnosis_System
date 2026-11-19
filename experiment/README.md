# Experiment assets

Research code, datasets, and reports are grouped under this directory:

- `src/` contains Python runners, reusable analysis code, host adapters, and
  archived research tools.
- `data/` contains local datasets and data manifests. Large data files are
  excluded from Git.
- `report/` contains report documents, score tables, and generated run outputs.

Run commands from the repository root so paths such as
`experiment/data/qtdb` and `experiment/report/outputs` resolve consistently.
The firmware project remains in `firmware/esp32`. Upstream repositories remain
isolated as modules and their source files are not edited here.
