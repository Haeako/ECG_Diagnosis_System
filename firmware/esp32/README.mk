# ECG Diagnosis System — Hướng dẫn tham gia dự án

Tài liệu này dành cho thành viên mới làm việc với firmware ESP32 ECG. File
`README.md` mô tả chi tiết implementation; tài liệu này tập trung vào cách cài
đặt, chạy, kiểm tra và tìm đúng nơi cần sửa.

## 1. Mục tiêu hiện tại

Thiết bị phải hoạt động độc lập kể cả khi không có điện thoại:

```text
ADC ~360 Hz
→ lọc cơ bản
→ Pan-style QRS
→ RR/BPM
→ Orphanidou SQI shadow
→ Extra Trees INT8 AF screening
→ lưu SD
→ gửi batch/event qua BLE nếu có kết nối
```

Điện thoại/laptop là gateway và nơi quan sát dữ liệu. Cloud/máy tính dành cho
phân tích dài hạn, review bất đồng và huấn luyện lại. Đây là prototype nghiên
cứu/sàng lọc, không phải thiết bị chẩn đoán lâm sàng.

## 2. Yêu cầu môi trường

- Windows PowerShell.
- ESP-IDF `>=6.0`; bản đã dùng để kiểm tra là ESP-IDF 6.0.1.
- Target `esp32`, board đã thử là ESP32-D0WD-V3, CPU 160 MHz, flash 4 MB.
- Python virtual environment ở `.venv` nếu muốn chạy gateway/benchmark.
- Gói Python BLE: `bleak>=0.22,<2`.
- Thẻ SD FAT; firmware không tự format hoặc xóa dữ liệu người dùng.

Mở ESP-IDF terminal rồi kiểm tra:

```powershell
idf.py --version
.\.venv\Scripts\python.exe --version
```

Cài dependency cho gateway nếu cần:

```powershell
.\.venv\Scripts\python.exe -m pip install -r tools\requirements-gateway.txt
```

## 3. Build, flash và monitor

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

Thay `COMx` bằng cổng thật. Sau health check, firmware phải ở `IDLE`; nó không
tự ghi. Bắt đầu/dừng recording bằng GPIO0 hoặc lệnh BLE `start`/`stop`.

Kỳ vọng khi boot:

- Chip flash được nhận là 4 MB, không còn cảnh báo image header 2 MB.
- SDHC mount thành công nếu thẻ hợp lệ.
- State machine đi `FAULT → PREPARE → IDLE`.
- Không có `IDLE → RECORD` cho tới khi có lệnh rõ ràng.

## 4. Cấu trúc code cần biết

| Khu vực | Trách nhiệm |
|---|---|
| `main/main.c` | Bootstrap |
| `main/task/Core0/StateMachine/` | Quyền sở hữu trạng thái INIT/IDLE/RECORD và recovery |
| `main/task/Core1/ECGPipeline/` | ADC processing, SD writer, ghép SQI/AF |
| `main/task/Core1/ECGPipeline/ExFeature/` | Filter, R-peak, RR và BPM |
| `main/components/sqi_gate/` | Orphanidou, template, bSQI, Zhao, Kramer, XQRS và benchmark |
| `main/components/af_model/` | Extra Trees INT8 và AF runtime |
| `main/components/esp_ble/` | GATT protocol và notification |
| `main/task/Core0/BLETransmit/` | Hàng đợi và truyền BLE |
| `main/drivers/sd_spi/` | SD/FAT và file handling |
| `main/components/metrics/` | Profiling, queue, latency và stack metrics |
| `tools/` | BLE gateway, DB inspection và profiling |
| `benchmark_results/` | Artifact, manifest, report và executable kiểm chứng |
| `visualizations/` | ECG CSV viewer |

Core 1 sở hữu đường xử lý ECG theo mẫu. Core 0 chạy supervisor, SD writer,
NimBLE và BLE transmitter. Không để SD hoặc BLE blocking trong đường ADC.

## 5. Semantics dữ liệu

CSV SD có các trường chính:

```text
timestamp_ms, raw_adc, centered, notch_49_51hz, bandpass_5_18hz,
ma_qrs_100ms, threshold, is_r_peak, bpm,
sqi_score, sqi_status, sqi_rule_mask, sqi_hr_bpm, sqi_max_rr_ms,
sqi_rr_ratio_x1000, sqi_template_corr_x1000, sqi_window_id,
sqi_finalize_us, af_valid, af_probability_x10000, af_score,
af_raw_prediction, af_confirmed, af_window_id, af_event_id,
af_new_window, af_new_event
```

SQI status:

| Giá trị | Ý nghĩa |
|---:|---|
| 0 | UNUSABLE/BAD |
| 1 | USABLE/GOOD |
| 2 | INSUFFICIENT/chưa đủ dữ liệu |

Không gộp `INSUFFICIENT` với `BAD` khi tính metric. `sqi_score=255` biểu thị
chưa đủ dữ liệu. SQI đang shadow-only: nó ghi log/event nhưng chưa chặn AF.

Nếu SQI là BAD, BPM/R-peak thoáng qua trên input floating không được xem là
nhịp tim hợp lệ. `af_valid=0` nghĩa là chưa được phép diễn giải AF output.

## 6. AF runtime

- Model: 80-tree Extra Trees INT8.
- Input: 30 RR liên tiếp.
- Stride: 15 RR.
- Features: pNN50, Shannon entropy 5-bin, SD2, SD1/SD2, median RR.
- Confirmation: ít nhất 3 positive trong 4 cửa sổ gần nhất.
- RR ngoài 250–2500 ms reset history.

Không gọi Ho[NeuroKit] là mô hình AF. Ho[NeuroKit] đánh giá chất lượng RR đứng trước
AF classifier. Candidate production hiện tại vẫn là AF Extra Trees; SQI hard
gate chưa được bật.

## 7. Chạy BLE gateway

```powershell
.\.venv\Scripts\python.exe tools\edge_gateway.py `
  --name ECG-Monitor `
  --database benchmark_results\hardware_gateway.sqlite3 `
  --duration 30
```

Đọc kết quả SQLite:

```powershell
.\.venv\Scripts\python.exe tools\inspect_gateway_db.py `
  benchmark_results\hardware_gateway.sqlite3
```

Không cần kiểm tra pairing thủ công trước. Khi debug, ưu tiên sequence gap,
packet count, event flags và SQI/AF semantics.

## 8. Quan sát CSV SD

Mở `visualizations/ecg-csv-viewer-preview.html` hoặc dùng source fragment
`visualizations/ecg-csv-viewer.html`, sau đó chọn CSV trong:

```text
D:\DOC\ECG_Diagnosis_System\data\record
```

Viewer hiển thị raw/filtered signal, R-peak và ba trạng thái SQI riêng biệt.
Một tín hiệu nhìn đẹp chưa đủ để kết luận gate sai: cần kiểm tra
`sqi_rule_mask`, sample count, window boundary và template correlation.

## 9. Smoke tests

Các executable host đã có thể chạy trực tiếp:

```powershell
.\benchmark_results\af_runtime_host_test.exe
.\benchmark_results\sqi_runtime_shadow_test.exe
.\benchmark_results\sqi_compare_20261016\sqi_bsqi_host_test.exe
.\benchmark_results\sqi_compare_20261016\sqi_kramer_host_test.exe
```

Sau khi sửa model/SQI, cần chạy test tương ứng và `idf.py build`. Không xem
file executable cũ là bằng chứng cho source mới nếu chưa rebuild.

## 10. Các protocol không được trộn số

1. QTDB MLII: đánh giá độ chính xác R-peak so với annotation `atr`.
2. SQI internal holdout: nhãn usability của production RR detector.
3. CinC 2011 Set A: nhãn acceptable/unacceptable của expert.
4. AFDB: downstream AF classification.
5. AFDB + NSTDB: stress test với muscle-artifact burst.
6. Hardware capture: kiểm tra thiết bị/transport, không phải clinical metric.

Không so sánh trực tiếp BA/AUC giữa các protocol có dataset hoặc nhãn khác.
Luôn ghi dataset, lead, window, split, threshold và coverage bên cạnh metric.

## 11. Trạng thái phương pháp SQI

- Orphanidou/template: đang chạy shadow trên ESP32.
- bSQI/XQRS: đã parity và benchmark, chưa production.
- Ho[NeuroKit]: NeuroKit 0.2.13 `ho2025`, UNSW + NeuroKit; đã chạy trực tiếp trên CinC2011.
- RR-agreement Pan+XQRS: prototype downstream trước đây; khác implementation Ho[NeuroKit].
- Zhao[NeuroKit]: NeuroKit 0.2.13 `zhao2018`, `approach="simple"`; đã chạy trực tiếp trên CinC2011.
- Zhao-fuzzy nguyên bản: chưa reproduce đầy đủ.
- Zhao-6: extension thử nghiệm, không phải kết quả paper.
- Kramer full: quá chậm cho production.
- Clifford-5 RBF-SVM: research baseline tốt, chưa phù hợp ESP32 hiện tại.
- bSQI→Zhao: đã loại khỏi shortlist production.

## 12. Quy tắc làm việc

- Không sửa/xóa artifact hoặc dataset không thuộc task hiện tại.
- Không format SD tự động.
- Giữ state machine là nơi duy nhất quyết định mode.
- Không block đường ADC bằng SD, BLE, log hoặc FFT dài.
- Không dùng `bandpass_5_18hz` để diễn giải ST/T morphology.
- Thay đổi feature/model phải có golden-vector/parity test.
- Thay đổi protocol phải tạo manifest mới; không ghi đè kết quả cũ.
- Báo cáo rõ `reproduced`, `adapted`, `prototype` hoặc `not completed`.
- Không gọi kết quả screening là clinical validation.

## 13. Việc ưu tiên tiếp theo

1. Bắt đầu SQI window tại mẫu ADC đầu tiên để bỏ false BAD lúc startup.
2. Kết nối AD8232/electrode và thu tín hiệu thật.
3. Tăng stack State task và lặp fault-path test.
4. Chỉnh timer chính xác 360 Hz.
5. Tìm pipeline latency outlier vượt deadline.
6. Chạy soak test nhiều giờ với BLE, SD rotation và fault injection.
7. Benchmark Ho[NeuroKit] hoặc biến thể detector đã định danh trước khi quyết định hard gate.

Đọc thêm:

- `README.md`
- `REPORT_METRICS.md`
- `../../reports/README.md`
- `../../outputs/af_detection/firmware_rr/REPORT.md`
- `../../outputs/af_detection/int8_deployment/REPORT.md`
- `../../outputs/ecg_quality_clifford_rahman_v1/`

Artifact cũ nằm tại `D:\DOC\ECG_PAPER\temp\benchmark_results` và
`D:\DOC\ECG_PAPER\temp\out`. Mục lục báo cáo tổng hợp là `REPORT_METRICS.md`;
dữ liệu của lần chạy mới nằm tại `benchmark_runs/cinc2011_20261019/`.
Script tái lập: `tools/run_cinc2011_verified.py` (dùng Python ở virtualenv
cấp project, `../../.venv/Scripts/python.exe`). Chọn `--output` mới khi chạy lại.

## 14. BPM/RR và toàn bộ AF — bản hoàn thiện 19/10/2026

Báo cáo: [REPORT_BPM_AF.md](benchmark_runs/non_sqi_20261019/REPORT_BPM_AF.md).
Gồm toàn bộ lịch sử BPM/QRS, 8 cấu hình AF frozen + INT8, replay current C
clean/12/6/0 dB, BPM display thật, RR consecutive-pair, chi phí host và
checklist hardware còn thiếu. Không chạy thêm SQI.

```powershell
$env:PYTHONDONTWRITEBYTECODE="1"
..\..\.venv\Scripts\python.exe tools\test_complete_bpm_af_report.py
..\..\.venv\Scripts\python.exe tools\complete_bpm_af_report.py --phase all
```

Runner ghi vào `benchmark_runs/non_sqi_20261019/`; chạy lại sẽ tái tạo các
artifact của runner tại đó, không sửa dataset/model/firmware. Các artifact
gốc ở project `outputs` và thư mục archive được giữ nguyên. Không chạy
builder AF firmware-RR cũ để làm kết quả mới: biểu thức timestamp `uint32 * fs`
của nó đã được xác nhận có overflow trên bản ghi dài; runner mới đổi kiểu
trước phép nhân và kiểm tra monotonicity.

## 15. Survey BUTQDB — hoàn tất 20/10/2026

[Báo cáo QRS](benchmark_runs/butqdb_20261020/REPORT.md):
28 cấu hình single-lead, 35.773 cửa sổ, 15 subjects; 5-fold subject-disjoint.
Chỉ chấm khả năng sử dụng QRS: class1+2 good, class3 bad. Giữ fixed gates,
từng SQI đơn, feature fusion Logistic/RBF-SVM và cascade. Không có Clifford-84
vì BUTQDB chỉ có một kênh ECG. Các kết quả ngoài phạm vi được lưu trong
[streck.md](benchmark_runs/butqdb_20261020/streck.md); không xóa artifact.

Dataset nguồn: `D:\DOC\ECG_Diagnosis_System\butqdb`.
Artifact: `benchmark_runs/butqdb_20261020/`.
Đọc `method_registry_qrs.json` và `metrics_qrs.csv` cho phạm vi chính;
`final_audit.json` ghi kiểm chứng cùng hashes. Registry/metrics gốc vẫn giữ
đủ lần chạy cũ để truy vết phần đã đưa vào streck.md.

Kiểm tra artifact có sẵn từ thư mục firmware:

```powershell
$env:PYTHONDONTWRITEBYTECODE="1"
..\..\.venv\Scripts\python.exe tools\test_butqdb_survey.py
..\..\.venv\Scripts\python.exe tools\audit_butqdb_progress.py
..\..\.venv\Scripts\python.exe tools\report_butqdb_survey.py
```

Lệnh trích xuất và huấn luyện lại nằm trong báo cáo. Runner phụ thuộc
NeuroKit vendor và EP Limited DLL tại
`D:\DOC\ECG_PAPER\temp\benchmark_results`; pyarrow nằm riêng trong
`.cache/butqdb_deps`. Không thay dữ liệu/thuật toán rồi resume cache cũ.
Đây là benchmark offline trên host, không phải parity hoặc benchmark ESP32.
Không dùng quality labels của BUTQDB để tạo accuracy AF/BPM.

