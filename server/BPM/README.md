# BPM — feature/experiment

Chỉ dùng để tạo và tái tạo số liệu báo cáo. Chạy từ thư mục gốc, dùng venv riêng:

```powershell
server/BPM/.venv/Scripts/python.exe server/BPM/prepare_dataset.py
server/BPM/.venv/Scripts/python.exe server/BPM/eval.py --report --output outputs/BPM/new_report
server/BPM/.venv/Scripts/python.exe server/BPM/eval.py --help
```

`--report` dùng đầy đủ cohort và model đã chốt từ cache đã audit, đối soát metrics, cập nhật bảng Markdown rồi lưu CSV/plot. Không suy luận hoặc train. `--from-cache` chỉ xuất metrics/plot của lựa chọn hiện tại. Bỏ cả hai cờ để chạy module theo input chuẩn bị. Output phải là thư mục mới; không tự download dữ liệu/checkpoint.

EDB 57 record, 22 điều kiện Original/BW/MA/EM và mixed. Backend host xử lý toàn record; backend C reset context10s/hop5s. Dùng `--backend c --report` để tái tạo bảng C/ESP32. F1 ghép một-một trong ±50/150ms; MAE/RMSE BPM không bỏ lỗi theo tolerance. `--records`/`--conditions` chỉ dùng cho diagnostic, không đi cùng `--report`.

Xem [quy trình chung](../../README.md) và [bảng số liệu](../../docs/BENCHMARK_SUMMARY_TABLES.md).
