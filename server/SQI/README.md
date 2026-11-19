# SQI — feature/experiment

Chỉ dùng để tạo và tái tạo số liệu báo cáo. Chạy từ thư mục gốc, dùng venv riêng:

```powershell
server/SQI/.venv/Scripts/python.exe server/SQI/prepare_dataset.py
server/SQI/.venv/Scripts/python.exe server/SQI/eval.py --report --output outputs/SQI/new_report
server/SQI/.venv/Scripts/python.exe server/SQI/eval.py --help
```

`--report` dùng đầy đủ cohort và model đã chốt từ cache đã audit, đối soát metrics, cập nhật bảng Markdown rồi lưu CSV/plot. Không suy luận hoặc train. `--from-cache` chỉ xuất metrics/plot của lựa chọn hiện tại. Bỏ cả hai cờ để chạy module theo input chuẩn bị. Output phải là thư mục mới; không tự download dữ liệu/checkpoint.

BUTQDB 35.773 cửa sổ, 18 record/15 người; mặc định đủ 9 method. `--methods Ho "Kramer C" templatematch` chọn module. Native Orphanidou/Zhao/bSQI dùng primary-peak cache có checksum, không dùng peak annotation. NeuroKit0.2.13 frozen theo đường dẫn vendor trong modules/_runtime.py. `metrics.py` tính AUC valid/common và coverage; score invalid giữ NaN. `--limit` là diagnostic và không dùng với `--report`.

Xem [quy trình chung](../../README.md) và [bảng số liệu](../../docs/BENCHMARK_SUMMARY_TABLES.md).
