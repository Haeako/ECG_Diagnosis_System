# Chọn dataset và giải thích AF từ RR trên server

Entry point: [run_selected_dataset_af_xai.py](../experiment/src/experiments/af_detection/run_selected_dataset_af_xai.py).

```text
Dataset và record đã chọn → waveform mV → AFE/ADC/notch như replay trước
→ SWT–SepConv → R-peaks server → 30 RR → 5 đặc trưng
→ Extra Trees INT8 của device đã freeze
→ SHAP từng feature và phép thay một đầu vào → console + HTML
```

Script bắt buộc chọn một dataset và các record cần chạy, không lặp qua các dataset khác. Background TRAIN đã khóa chỉ được đọc dưới dạng bảng đặc trưng để tham chiếu SHAP; không chạy lại waveform của bộ khác. Không train model hoặc LR fusion.

## Cách chạy

Chạy từ `D:\DOC\ECG_Diagnosis_System`.

Liệt kê record có đủ waveform:

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_selected_dataset_af_xai.py --dataset afdb --list-records
```

Chạy một record AFDB và mở trang giải thích:

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_selected_dataset_af_xai.py --dataset afdb --records 04936 --duration-seconds 180 --open-report
```

Ví dụ SHDB-AF đã xuất:

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_selected_dataset_af_xai.py --dataset shdb-af --records 002 --start-seconds 8610 --warmup-seconds 90 --duration-seconds 180 --open-report
```

Chọn mọi record waveform có sẵn của riêng LTAFDB:

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_selected_dataset_af_xai.py --dataset ltafdb --all-records --duration-seconds 180 --no-waveform-plots
```

`--all-records` chọn mọi record, nhưng thời lượng của mỗi record vẫn theo tham số, không mặc định chạy toàn bộ bản ghi 24 giờ.

| Tham số | Ý nghĩa |
|---|---|
| `--dataset afdb\|ltafdb\|shdb-af` | Bắt buộc chọn một dataset |
| `--records 002 018` hoặc `--all-records` | Chọn record cụ thể hoặc mọi record waveform của dataset đó |
| `--data-dir PATH` | Đổi thư mục nguồn của dataset đã chọn |
| `--start-seconds 8610` | Bắt đầu clip, tính cả warmup |
| `--warmup-seconds 90` | Context đầu clip; mặc định 90 giây |
| `--duration-seconds 180` | Phần giải thích sau warmup; mặc định 180 giây |
| `--lead 0` | Kênh ECG |
| `--console-limit 10` | In 10 ca đầu; đặt 0 để in tất cả; HTML giữ mọi ca |
| `--no-waveform-plots` | Bỏ PNG ECG/RR để giảm thời gian và dung lượng |
| `--output PATH` | Thư mục mới, không ghi đè output đã tồn tại |
| `--open-report` | Mở HTML bằng trình duyệt mặc định |

Warmup và thời lượng đánh giá phải là bội số 10 giây. Thiếu waveform hoặc thiếu thời lượng sẽ báo lỗi, không tự đổi dataset hay cắt ngắn dữ liệu.

## Giải thích cho từng ca

Mỗi ca là một inference mới trên **30 RR thực sự đi vào mô hình**. Trang HTML có bộ chọn cửa sổ, bộ lọc AF/non-AF, ECG với R-peaks, biểu đồ 30 RR và các thông tin:

- Giá trị cả năm đặc trưng, số INT8 sau lượng tử và SHAP có dấu theo điểm phần trăm.
- Điểm nền cộng tổng SHAP bằng đúng điểm AF của predictor C.
- Quy tắc raw bằng **score nguyên ≥10.240 trên tổng 20.400**, không thay bằng `p >= 0,5`.
- Chạy lại chính predictor khi thay riêng từng feature bằng trung vị TRAIN: điểm đổi bao nhiêu và có đổi nhãn không.
- Trạng thái confirmed 3/4 riêng với raw; đánh dấu inference đã bị cập nhật/reset bởi các peak sau trong cùng khối.

Ví dụ SHDB-AF 002, cửa sổ nguồn 8693,411–8705,189 s:

| Feature | Giá trị thật | SHAP |
|---|---:|---:|
| Median RR | 383,00 ms | +34,837 điểm % |
| pNN50 | 41,38% | +14,067 điểm % |
| SD2 | 52,55 ms | +1,377 điểm % |
| SD1/SD2 | 1,0771 | +0,766 điểm % |
| Entropy RR | 1,9657 bit | +0,719 điểm % |

`24,8738% + 51,7662 điểm % = 76,6400%`. Score **15.636 ≥10.240**, nên model kết luận **AF** cho cửa sổ này.

Giữ nguyên bốn feature còn lại và thay median RR từ **383 ms** thành trung vị TRAIN **836 ms**: score giảm còn **5.549**, đổi sang **non-AF**. Chỉ thay pNN50 thành trung vị TRAIN **6,90%**: score còn **8.372**, cũng đổi sang non-AF. Phép thay đầu vào này khác với SHAP và không phải can thiệp sinh lý; các feature tương quan nên tổ hợp mới có thể không tương ứng một chuỗi RR thật.

Nhãn tham chiếu TP/TN/FP/FN đối chiếu nhịp tại **cuối khối nguồn**, không phải nhãn thuần của toàn cửa sổ RR. Trang hiển thị phạm vi nhãn và chuyển nhịp. Nhãn chưa biết giữ là unknown và không tham gia quyết định AF hay tính SHAP.

## Output và ví dụ đã chạy

[Mở ví dụ giải thích 29 cửa sổ của SHDB-AF 002](../visualize/results/outputs/AFib/selected_af_xai/shdb002_feature_explanation_20261104_final/explanations.html).

Mặc định mỗi lần chạy lưu số liệu dưới `outputs/AFib/selected_af_xai/`.
HTML và ảnh nằm trong thư mục tương ứng dưới
`visualize/results/outputs/AFib/selected_af_xai/`:

- `explanations.html`: trang offline chứa mọi ca, ảnh được nhúng vào HTML.
- `cases.json`: lời giải thích đầy đủ, input và các phép thay feature.
- `explanations.csv`: feature và SHAP từng inference window.
- `block_decisions.csv`: trạng thái cuối từng khối, kể cả khối thiếu 30 RR.
- `events/*.npz`: peak, feature, score và provenance theo record.
- `events/*.json`, `protocol.json`, `models.json`: cấu hình, nguồn và hash.
- `summary.json`, `status.json`: tóm tắt và trạng thái chạy.

## File Python liên quan pipeline server

| File | Vai trò |
|---|---|
| [run_selected_dataset_af_xai.py](../experiment/src/experiments/af_detection/run_selected_dataset_af_xai.py) | Chọn dataset/record, điều phối pipeline và xuất kết quả |
| [rr_af_dataset.py](../experiment/src/experiments/af_detection/rr_af_dataset.py) | Load clip WFDB đã chọn, đổi sang mV, đọc nhãn và hash |
| [fusion_sync/server_rhythm.py](../experiment/src/experiments/af_detection/fusion_sync/server_rhythm.py) | SWT–SepConv → R-peaks, RR/BPM và template SQI |
| [fusion_sync/server_rr_af.py](../experiment/src/experiments/af_detection/fusion_sync/server_rr_af.py) | Gọi runtime RR và Extra Trees INT8 của device đã freeze |
| [exact_rr_shap.py](../experiment/src/experiments/af_detection/exact_rr_shap.py) | Tính Shapley đủ 32 tập con của năm feature |
| [rr_af_explanation.py](../experiment/src/experiments/af_detection/rr_af_explanation.py) | Giải thích tiếng Việt, phép thay feature, console và HTML |
| [fusion_sync/device.py](../experiment/src/experiments/af_detection/fusion_sync/device.py) | Tái tạo waveform qua AFE/ADC/notch như replay trước; adapter cũng chạy C device để giữ input thống nhất |

Các file thuộc pipeline server và fusion rộng hơn, **không được gọi bởi script chọn dataset này**:

| File | Vai trò |
|---|---|
| [fusion_sync/server.py](../experiment/src/experiments/af_detection/fusion_sync/server.py) | ECGFounder một lead, đầu gốc 150 nhãn; AF index 5 |
| [fusion_sync/alignment.py](../experiment/src/experiments/af_detection/fusion_sync/alignment.py) | Ghép output theo thời điểm sẵn có |
| [fusion_sync/learning.py](../experiment/src/experiments/af_detection/fusion_sync/learning.py) | LR fusion, ngưỡng validation, predict và fallback |
| [run_published_fusion.py](../experiment/src/experiments/af_detection/run_published_fusion.py) | Benchmark hai nhánh gốc và train LR |
| [run_server_rr_xai.py](../experiment/src/experiments/af_detection/run_server_rr_xai.py) | Lượt RR AF/XAI cũ dùng manifest SHDB đã khóa |

Năm đặc trưng và lượng tử hóa thực tế nằm trong C: `firmware/esp32/main/components/af_model/af_runtime.c` và `af_model_int8.c`. Python gọi chúng qua `fusion_sync/server_rr_af_host.c`, không thay bằng công thức Python gần đúng. Source upstream SWT–SepConv vẫn ở module riêng.

## Phạm vi

Giải thích áp dụng cho **điểm AF thô của Extra Trees từ RR**, không phải hình thái ECGFounder, lịch sử confirmed 3/4 hay LR cuối cùng. Đây là host replay với resample offline và network giả lập 0; availability không phải latency thu nhận và truyền thông vật lý. Clip chọn tùy ý không tự trở thành holdout hay benchmark lâm sàng.
