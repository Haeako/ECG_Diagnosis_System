# MLP fusion và latency toàn bộ pipeline server

Hoàn tất ngày 05/11/2026. Thử nghiệm MLP fusion thay cho LR trên cùng 13 feature, giữ nguyên trọng số các model base và artifact LR cũ.

## 1. Giao thức MLP

- SHDB-AF ECG1: giữ nguyên split 55/19/19 bệnh nhân; 3.229 hàng paired train, 1.022 validation, 1.024 test. Tổng test có 1.026 hàng; hai hàng thiếu device dùng fallback server.
- MLP **13 → 16 → 8 → 1**, ReLU, output xác suất AF; **369 tham số**. L-BFGS, alpha 0,01, seed 42, trọng số mẫu balanced chỉ tính từ train. Giữ cả feature tuổi output device.
- Imputer/scaler chỉ fit train. Kiến trúc cố định; ngưỡng chọn bằng BA trên validation theo cùng quy tắc của LR. Không chọn model hoặc ngưỡng theo test.
- Đã hội tụ sau **1965 vòng**, không có cảnh báo. Giới hạn tối ưu được tăng từ 1.000 lên 10.000 vì lần đầu chạm giới hạn; kiến trúc và alpha không đổi. Số liệu lần chưa hội tụ nằm ở thư mục thử ban đầu, không dùng làm kết quả chính.

## 2. Kết quả cùng 1.024 hàng test

| Model | N chung | BA | Sensitivity | Specificity | F1 AF | AUC | Ngưỡng |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Device | 1024 | 85.45% | 85.87% | 85.03% | 75.37% | 0.9211 | 0.6803 |
| ECGFounder | 1024 | 90.00% | 92.19% | 87.81% | 81.44% | 0.9589 | 0.80133837 |
| LR | 1024 | 92.02% | 91.45% | 92.58% | 86.16% | 0.9616 | 0.65767098 |
| MLP | 1024 | 89.05% | 89.22% | 88.87% | 80.94% | 0.9376 | 5.9255391e-08 |

MLP cấu hình này đạt BA **89.05%**, thấp hơn LR **2.97 điểm phần trăm**. Bootstrap ghép cặp 2.000 lần theo 19 bệnh nhân: CI95% của BA(MLP) − BA(LR) là **[-5.11; -1.06] điểm**. Tiếp tục giữ LR cho pipeline hiện tại. Đây là kết quả của cấu hình MLP đã thử, chưa đại diện cho mọi kiến trúc MLP.

Ngưỡng MLP nhỏ và khác xa 0,5; được chọn trên validation, phản ánh phân bố score của model này. Không đọc score MLP như xác suất đã được hiệu chuẩn. Tính cả fallback, MLP đạt coverage 100%, BA 89.00%, F1 AF 80.81% trên 1.026 hàng.

## 3. Latency đo trực tiếp trên host

**Phần được bấm giờ:** chạy hàm xử lý trên buffer ECG 10 giây sau thu nhận/notch đã có đủ trong RAM, đến khi có kết quả fusion/fallback và serialize JSON. Bao gồm ECGFounder preprocessing/inference, SWT preprocessing/inference/localizer, RR/BPM/template SQI, đồng bộ và tạo feature, imputer/scaler/classifier, routing và JSON. Đây là **thời gian tính toán chủ động**: telemetry device đã được replay và snapshot được lấy ở deadline +1 giây; hàm benchmark không chờ thật để telemetry đến deadline đó.

Chạy **batch 1**, **2 thread**, 57 đoạn thuộc 19 bệnh nhân test, 18 cửa sổ/đoạn; đo đủ **1.026 cửa sổ cho từng model**, tổng **2.052 lượt pipeline**. Mỗi cửa sổ chạy lại model base cho cả LR và MLP; đảo thứ tự LR/MLP luân phiên để giảm thiên lệch thứ tự. Ba cửa sổ warmup được loại khỏi thống kê. Dùng perf_counter đo wall time, không cộng p95 của các thành phần để suy ra p95 tổng.

| Đường xử lý | N | Mean ms | p50 ms | p95 ms | p99 ms | Max ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Server AF + LR fusion | 1026 | 77.67 | 72.23 | 100.98 | 147.82 | 295.96 |
| Server AF + MLP fusion | 1026 | 77.34 | 72.14 | 102.69 | 143.51 | 211.03 |
| Server AF + MLP + RR INT8 + SHAP | 1026 | 81.28 | 76.79 | 104.64 | 148.95 | 213.64 |

Do đó, không dùng 72 ms làm độ trễ thực từ mẫu ECG cuối đến output của policy hiện tại. Theo lịch snapshot **cuối khối +1 giây**, độ trễ lập lịch được tính riêng bằng `max(1000 ms, base_ms) + (core_wall_ms - base_ms)`, lưu ở [scheduled_latency.json](../experiment/report/benchmark_runs/af_fusion_mlp_latency_20261105_final/scheduled_latency.json). Đây là số suy ra theo policy replay, không phải phép đo live có chờ/network. Từ đầu cửa sổ đến quyết định còn phải tính cả khoảng thu 10 giây.

Theo công thức này, LR có p50/p95 **1.008,71/1.011,60 ms**, MLP **1.008,75/1.011,55 ms** sau khi khối ECG đầy. Tất cả lượt inference base đều hoàn tất trước deadline 1 giây. Với thời gian thu cửa sổ, quyết định đầu ra tương ứng khoảng **11,01 giây tính từ đầu cửa sổ**, chưa tính truyền thông.

Đường có RR/SHAP được đo trực tiếp trong cùng lượt MLP, sau khi kết quả AF core đã sẵn sàng. Nó thêm streaming runtime C 30 RR → Extra Trees INT8 → exact SHAP với 64 background train và 32 coalition. Cache lời giải thích theo feature window; chỉ tính lại khi feature window đổi. Score RR/SHAP vẫn là nhánh giải thích phụ, chưa được thêm vào 13 input fusion.

### Phân rã latency

| Thành phần | LR p50 ms | LR p95 ms | MLP p50 ms | MLP p95 ms |
| --- | ---: | ---: | ---: | ---: |
| Preprocessing ECGFounder | 1.59 | 2.29 | 1.58 | 2.30 |
| ECGFounder inference | 44.70 | 66.12 | 44.48 | 64.05 |
| Preprocessing SWT | 1.74 | 2.53 | 1.74 | 2.54 |
| SWT–SepConv inference | 14.52 | 20.76 | 14.58 | 20.53 |
| Localizer + RR/BPM + template SQI | 0.67 | 1.02 | 0.67 | 1.04 |
| Đồng bộ + tạo feature | 1.36 | 2.17 | 1.35 | 2.13 |
| Imputer + scaler + classifier + fallback | 7.23 | 9.60 | 7.27 | 9.72 |
| Serialize JSON | 0.06 | 0.11 | 0.07 | 0.10 |

Nhánh phụ: RR/INT8 p50 **0.079 ms**, p95 **0.125 ms**; SHAP/cache p50 **5.205 ms**, p95 **6.566 ms**, tính trên tất cả cửa sổ, gồm các hàng chưa đủ RR. Có 807/1026 cửa sổ có output RR và 698 lần tính lại SHAP.

Trong phép đo này, RR server reset theo từng đoạn và chỉ nhận peak của 18 khối đánh giá; 90 giây warmup nguồn được dùng cho firmware. Coverage RR nêu trên mô tả lịch khởi động này; không dùng nó thay kết quả cohort của benchmark XAI trước.

### Giới hạn của phép đo

- Không tính thời gian thu cửa sổ 10 giây, BLE/network, đọc dataset, mô phỏng firmware/ADC, ghi database hoặc render web. Đây là latency tính toán server từ buffer đầy đến JSON, không phải latency live ESP32→web.
- Replay dùng snapshot cố định **cuối khối + 1.000 ms**. Quy tắc chờ này không được sleep trong benchmark; không cộng thêm toàn bộ thời gian inference vào 1.000 ms khi inference đã hoàn tất trước snapshot.
- Model initialization được lưu riêng ở `model_loading.json`; ECGFounder load khoảng 4.97 giây trong lượt này. Dùng mmap checkpoint để giảm đỉnh RAM; strict load, đầu 150 nhãn và trọng số giữ nguyên.
- Host: Intel64 Family 6 Model 165 Stepping 2, GenuineIntel, 8 logical CPU, Windows, RAM 7.84 GiB. Snapshot trong phép đo có mức dùng RAM **93%**; các giá trị trên phản ánh điều kiện máy ở lượt chạy này. Không suy ra latency ESP32 hoặc server phần cứng khác.
- Thời gian classifier trong bảng gồm imputer/scaler, pandas và fallback của adapter; không phải riêng phép nhân ma trận MLP/LR.

## 4. Đối soát và mã nguồn

Đối soát độc lập **PASS**: tính lại confusion matrix/BA/F1/AUC từ dự đoán đã lưu; kiểm tra imputer/scaler đúng train; tính lại quantile từ 2.052 mẫu timing; kiểm tra ADC input khớp replay gốc, streaming RR khớp replay C toàn phiên, SHAP cộng lại đúng score, và hash model/code/input không đổi.

- [Runner train + profile](../experiment/src/experiments/af_detection/run_mlp_fusion_latency.py)
- [C adapter RR streaming](../experiment/src/experiments/af_detection/fusion_sync/server_rr_stream_host.c)
- [Protocol](../experiment/report/benchmark_runs/af_fusion_mlp_latency_20261105_final/protocol.json)
- [Kết quả phân loại](../experiment/report/benchmark_runs/af_fusion_mlp_latency_20261105_final/training_summary.json)
- [Từng dự đoán](../experiment/report/benchmark_runs/af_fusion_mlp_latency_20261105_final/predictions.csv)
- [Latency tổng hợp](../experiment/report/benchmark_runs/af_fusion_mlp_latency_20261105_final/latency_summary.json)
- [2.052 mẫu timing](../experiment/report/benchmark_runs/af_fusion_mlp_latency_20261105_final/latency_samples.csv)
- [MLP artifact](../experiment/report/benchmark_runs/af_fusion_mlp_latency_20261105_final/fusion_mlp.joblib)
- [Audit độc lập](../experiment/report/benchmark_runs/af_fusion_mlp_latency_20261105_final/independent_audit.json)

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_mlp_fusion_latency.py --threads 2
# Chạy mới: chọn --output khác. Tiếp tục cùng host/runtime: thêm --resume.
```
