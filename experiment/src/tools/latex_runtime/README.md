# Công cụ biên dịch LaTeX cục bộ

Tectonic 0.17.0 portable được tải từ release chính thức:
https://github.com/tectonic-typesetting/tectonic/releases/tag/tectonic%400.17.0

Binary, gói TeX tải về và thư viện Python dựng trang PDF được giữ cục bộ, bỏ qua trong Git.
Không cần thư mục này nếu đã có XeLaTeX từ TeX Live hoặc MiKTeX.

Biên dịch từ thư mục gốc workspace:

```powershell
$env:TECTONIC_CACHE_DIR = Join-Path $PWD 'experiment/src/tools/latex_runtime/cache'
experiment/src/tools/latex_runtime/tectonic.exe --untrusted --keep-logs --outdir experiment/report/latex_build experiment/report/BAO_CAO_KHOA_LUAN_ECG.tex
```

Lần đầu cần mạng để tải các gói TeX. File `.tex` chứa cả sơ đồ và tài liệu tham khảo.
