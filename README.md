# ECG Experiment Benchmarks

Nhánh **`feature/experiment`** chỉ phục vụ tạo và tái tạo các số liệu trong [bảng báo cáo](docs/BENCHMARK_SUMMARY_TABLES.md): BPM/QRS, SQI và AFib. ESP32 là backend benchmark C. Nhánh này không chứa ứng dụng production, BLE/acquisition, gateway hoặc web monitor.

```text
server/BPM/       prepare_dataset.py, eval.py, metrics.py, modules/
server/SQI/       prepare_dataset.py, eval.py, metrics.py, modules/
server/AF/        prepare_dataset.py, eval.py, metrics.py, modules/  (AFib)
server/modules/   tiện ích dùng chung
datasets/         dữ liệu gốc và prepared/<bài toán>/manifest.json
outputs/          CSV, predictions, protocol và trạng thái theo bài toán
visualize/results/ plot theo bài toán và lượt eval
firmware/         các module C và backend thử nghiệm ESP32
external/         dependency bên thứ ba, giữ nguyên source và không commit repo clone
```

Mỗi phương pháp có một file trong `modules/`; file bắt đầu bằng `_` là helper. Ba domain dùng các môi trường `.venv` riêng đã có tại workspace. Chạy lệnh từ thư mục gốc; mỗi `--output` phải là thư mục mới.

Tái tạo đầy đủ số liệu và plot đã chốt, đồng thời cập nhật Markdown:

```powershell
server/BPM/.venv/Scripts/python.exe server/BPM/eval.py --report --backend host --output outputs/BPM/report_host
server/BPM/.venv/Scripts/python.exe server/BPM/eval.py --report --backend c --output outputs/BPM/report_c
server/SQI/.venv/Scripts/python.exe server/SQI/eval.py --report --output outputs/SQI/report
server/AF/.venv/Scripts/python.exe server/AF/eval.py --report --output outputs/AFib/report
```

`--report` đọc cache đủ cohort đã audit, tính lại metrics, đối soát số đã chốt rồi lưu CSV, plot và báo cáo. Không chạy lại suy luận, train hoặc đo lại ESP32. Latency ESP32 trong bảng được giữ từ phép đo thiết bị đã hoàn tất. Nếu thiếu cache hoặc số liệu không khớp, lệnh dừng thay vì tự tạo số thay thế. Các cache/dataset/checkpoint nằm tại workspace và đang được ignore trong Git; clone nhánh không tự tải chúng về.

Để chuẩn bị dữ liệu và chạy suy luận mới khi được yêu cầu:

```powershell
server/BPM/.venv/Scripts/python.exe server/BPM/prepare_dataset.py
server/SQI/.venv/Scripts/python.exe server/SQI/prepare_dataset.py
server/AF/.venv/Scripts/python.exe server/AF/prepare_dataset.py
```

Bỏ `--report` và `--from-cache` để chạy module theo input đã chuẩn bị. `--from-cache` chỉ xuất metrics/plot của lựa chọn hiện tại, không công bố lại báo cáo. Chỉ `server/AF/eval.py --train-fusion` fit LR trên SHDB; nhãn AFDB không vào train hoặc chọn ngưỡng.

- **BPM/QRS:** EDB 57 record loại 33 source trùng QTDB, 22 điều kiện Original/BW/MA/EM và mixed; host full-record và C context10s/hop5s có giao thức riêng. F1 dùng tolerance ±50/150 ms; MAE/RMSE BPM không bỏ lỗi theo tolerance.
- **SQI:** BUTQDB 35.773 cửa sổ, 18 record/15 người; AUC và coverage giữ cohort rõ ràng. SQI là chỉ số phụ, không chặn AF.
- **AFib:** tám model trên cùng 18.470 cửa sổ thuần nhãn AFDB. RR heads nhận năm feature native đã audit; waveform models nhận ECG10s; fusion nhận 13 feature đồng bộ đã audit. LR đã train trên 93 người/294 session SHDB chuẩn bị, chưa phải toàn bộ thời lượng raw. Chưa xác minh disjoint với toàn bộ pretraining ECGFounder.

Chi tiết: [Python modules](server/README.md), [BPM](server/BPM/README.md), [SQI](server/SQI/README.md), [AFib](server/AF/README.md), [firmware benchmark](firmware/README.md). Báo cáo BPM chính: [REPORT.md](experiment/report/qrs_rr_bpm/REPORT.md).
