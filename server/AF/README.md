# AFib — feature/experiment

Chỉ dùng để tạo và tái tạo số liệu báo cáo. Chạy từ thư mục gốc, dùng venv riêng:

```powershell
server/AF/.venv/Scripts/python.exe server/AF/prepare_dataset.py
server/AF/.venv/Scripts/python.exe server/AF/eval.py --report --output outputs/AFib/new_report
server/AF/.venv/Scripts/python.exe server/AF/eval.py --help
```

`--report` dùng đầy đủ cohort và model đã chốt từ cache đã audit, đối soát metrics, cập nhật bảng Markdown rồi lưu CSV/plot. Không suy luận hoặc train. `--from-cache` chỉ xuất metrics/plot của lựa chọn hiện tại. Bỏ cả hai cờ để chạy module theo input chuẩn bị. Output phải là thư mục mới; không tự download dữ liệu/checkpoint.

Cohort chung 18.470 cửa sổ AFDB thuần nhãn. RR heads nhận năm feature native đã audit; ECGFounder/ConvNeXt chạy waveform10s; fusion LR nhận snapshot13 feature đồng bộ đã audit. Đây là benchmark từng module, không tự coi việc dùng lại feature là chạy mới toàn pipeline. Chỉ `--train-fusion` fit LR trên 93 người/294 session SHDB chuẩn bị, cutoff OOF5fold tách người; AFDB không vào fit/tune. SQI không hard gate. Chưa chứng minh disjoint với toàn bộ pretraining ECGFounder. `--limit`/`--train-fusion` không dùng với `--report`.

Xem [quy trình chung](../../README.md) và [bảng số liệu](../../docs/BENCHMARK_SUMMARY_TABLES.md).
