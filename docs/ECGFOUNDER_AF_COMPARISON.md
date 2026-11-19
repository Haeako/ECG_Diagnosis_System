# So sánh 7 model AF trên toàn bộ 6 record AFDB

> Phạm vi model nền: các số ECGFounder là kết quả lịch sử đã lưu; chưa xác minh đầy đủ cohort pretraining, vì vậy chưa chứng minh test độc lập với toàn bộ pretraining. Không thực hiện thêm lượt đánh giá độc lập khi provenance này chưa rõ.

Hoàn tất ngày **05/11/2026**, chạy inference với toàn bộ trọng số đã đóng băng.
LR là baseline độc lập trên RR. ECGFounder sử dụng checkpoint một lead được phát hành,
**giữ nguyên đầu 150 nhãn**, lấy sigmoid AF tại **index 5**.

## Dữ liệu và giao thức

- ECG1 của `04936`, `05091`, `05261`, `06453`, `07859`, `08434`.
- Tổng nguồn **60,3931 giờ**, **21.740 khối hoàn chỉnh 10 giây**; phần đuôi thiếu một khối tổng cộng **15,2 giây**.
- **21.616 khối thuần nhãn**: **6.518 AF/AFIB**, **15.098 không AF**. Lưu riêng **124 khối chuyển nhịp**; không có khối nhãn chưa rõ.
- Nhãn lấy tại đầu khối; loại các khối có thay đổi nhịp nằm nghiêm ngặt bên trong khối. Thay đổi đúng tại biên thuộc khối tiếp theo.
- Giữ trạng thái lọc và RR xuyên suốt record; reset ở đầu mỗi record. Khối thiếu 30 RR được tính vào coverage. Không cắt cố định 90 giây đầu.
- Bảng chính chấm cùng **18,470 hàng hợp lệ của cả 7 model**, đạt **85.45%** số khối thuần nhãn. Bảng riêng dùng hàng có output của từng model.

| Record | Khối nguồn | Thuần nhãn | AF | Không AF | Hợp lệ chung |
| --- | ---: | ---: | ---: | ---: | ---: |
| 04936 | 3,682 | 3,612 | 2,622 | 990 | 3,263 |
| 05091 | 3,682 | 3,669 | 4 | 3,665 | 3,166 |
| 05261 | 3,682 | 3,662 | 39 | 3,623 | 3,146 |
| 06453 | 3,330 | 3,317 | 32 | 3,285 | 2,254 |
| 07859 | 3,682 | 3,681 | 3,681 | 0 | 3,360 |
| 08434 | 3,682 | 3,675 | 140 | 3,535 | 3,281 |

## Đường xử lý đã chạy

1. Waveform chung: ECG vật lý → resample 360 Hz → AFE high-pass 0,5 Hz → ADC 12 bit → notch C hiện tại → ECG đã hiệu chỉnh.
2. Năm baseline RR: SWT–SepConv trên từng khối 10 giây → nối peak toàn record → gọi runtime C một lần/record → 30 RR, stride 15 → `pnn50`, `shannon_entropy`, `sd2_ms`, `sd1_sd2`, `median_rr_ms` → LR/RF/Extra Trees float/Extra Trees INT8/XGBoost.
3. Mỗi quyết định RR sử dụng cửa sổ đặc trưng hợp lệ mới nhất tính đến cuối khối. Cửa sổ RR có thể bắt đầu ở khối trước. Giữ nguyên quy tắc reset RR ngoài [250; 2.500] ms của firmware; không sửa hoặc nối bù RR.
4. ConvNeXtV2 FP32: trọng số cục bộ hiện có, preprocessing riêng từng khối, notch **60 Hz**, softmax AF index 1.
5. ECGFounder: load checkpoint **strict**, bộ lọc gốc **50 Hz và 0,67–40 Hz**, median baseline và chuẩn hóa từng cửa sổ; sigmoid nguyên **150 output**. Cả hai mạng waveform chạy batch 32, batch cuối theo số khối còn lại.

Ngưỡng float cố định **0,5**; INT8 dùng score nguyên **≥10.240**, score tối đa **20.400**. AUC INT8 dùng score/20.400; xác suất telemetry đã làm tròn được lưu riêng. Bảng chấm quyết định raw, trước xác nhận 3/4. Lượt so sánh này không áp dụng cổng loại theo SQI và không huấn luyện fusion.

Acquisition dùng resample polyphase offline theo đường replay hiện có; preprocessing riêng của hai mạng chỉ đọc khối đã hoàn tất. Mọi thời gian thực thi được lưu là thời gian **host CPU**.

## Kết quả gộp trên cùng các hàng hợp lệ

Nguồn: **21,740 khối**; thuần nhãn: **21,616 khối**; hợp lệ chung: **18,470 khối** (**85.45%** số khối thuần nhãn).

Coverage trong mỗi hàng là số khối thuần nhãn có output của riêng model chia cho tổng khối thuần nhãn. BA và AUC để trống khi cohort chỉ có một lớp thật.

| Model | Số hàng chung | BA | Sensitivity | Specificity | F1 AF | AUC | Coverage |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Logistic Regression RR | 18,470 | 90.67% | 83.78% | 97.56% | 88.66% | 0.9817 | 85.45% |
| Random Forest RR | 18,470 | 87.57% | 77.76% | 97.38% | 84.83% | 0.9825 | 85.45% |
| Extra Trees float RR | 18,470 | 92.38% | 88.78% | 95.99% | 89.98% | 0.9807 | 85.45% |
| Extra Trees INT8 RR | 18,470 | 92.14% | 88.57% | 95.71% | 89.60% | 0.9786 | 85.45% |
| XGBoost RR | 18,470 | 83.34% | 69.96% | 96.72% | 79.08% | 0.9783 | 85.45% |
| ConvNeXtV2 FP32 | 18,470 | 74.96% | 65.45% | 84.47% | 65.93% | 0.8705 | 100.00% |
| ECGFounder 1 lead, đầu gốc 150 nhãn | 18,470 | 96.28% | 96.36% | 96.21% | 94.26% | 0.9913 | 100.00% |

## Latency từng nhánh trên cohort AFDB riêng — 05/11/2026

Đo trực tiếp **720 cửa sổ/nhánh**, batch 1, CPU hai thread. Mỗi record lấy 120 khối 10 giây liên tiếp từ block `floor(0,10 × số khối hoàn chỉnh)`. Việc chọn đoạn chỉ đọc header. Đây là cohort đo thời gian riêng; các bảng accuracy và coverage toàn AFDB vẫn dùng đúng 18.470/21.616 hàng phía trên.

| Nhánh | N latency | p50 (ms) | p95 (ms) | p99 (ms) | Max (ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Logistic Regression RR | 720 | 20,57 | 29,19 | 50,13 | 96,70 |
| Random Forest RR | 720 | 25,94 | 43,46 | 74,45 | 135,05 |
| Extra Trees float RR | 720 | 24,56 | 32,35 | 36,98 | 43,47 |
| Extra Trees INT8 RR | 720 | 17,79 | 24,80 | 37,00 | 268,28 |
| XGBoost RR | 720 | 20,11 | 30,87 | 42,03 | 94,05 |
| ConvNeXtV2 FP32 | 720 | 140,66 | 181,40 | 231,87 | 272,82 |
| ECGFounder, đầu gốc 150 nhãn | 720 | 51,77 | 71,91 | 93,53 | 201,14 |

- Mỗi nhánh RR tự chạy lại SWT preprocessing/inference/localizer/RR/BPM/template SQI → runtime C tăng dần → classifier của nhánh. Runtime C giữ cả tác vụ ET INT8 sẵn có khi tính feature cho các model float; thời gian này nằm trong số đo. Lịch sử RR bắt đầu 90 giây trước đoạn đo, giữ xuyên ranh giới khối và tuân thủ reset RR lỗi của C.
- Timer tính từ waveform chung đã qua AFE/ADC/notch. Acquisition chung, đọc file, tải model, ba lượt warmup mạng, thu cửa sổ 10 giây và truyền thông nằm ngoài timer. Đây là thời gian tính toán host của từng nhánh, chưa gồm fusion.
- Nhánh RR có output trên **651/720 cửa sổ (90,42%)** của đoạn latency; hai mạng waveform có 100%. Percentile trong bảng dùng đủ 720 lần xử lý, gồm cả lần không có output RR. Các tỷ lệ này có mẫu số riêng với coverage accuracy AFDB.
- Các nhánh chạy tuần tự trong process mới; các job benchmark host và build ESP-IDF được tạm dừng trong lượt đo cuối. Một lần chạy LR ban đầu trùng build đã được lưu riêng và loại khỏi bảng.
- State tensor của SWT/ConvNeXt/ECGFounder lần lượt **2.809.864 / 3.067.272 / 123.496.960 B**, gồm trọng số và buffer mạng. Các số này chưa gồm activation hoặc framework; RSS/peak working set process được lưu riêng trong raw audit.

ECGFounder vẫn load strict, đầu 150 nhãn gốc, AF index 5; ConvNeXt dùng notch 60 Hz. Tất cả model và ngưỡng giữ nguyên. Audit độc lập kiểm tra lại **91 nhóm thống kê**, chênh lệch bằng 0, và **54 hash file** không đổi; năm tuyến RR cho peak, feature và output native giống hệt nhau, đồng thời khớp replay C một lần.

Nguồn: [protocol](../experiment/report/benchmark_runs/af_branch_latency_20261105/protocol.json), [từng lần đo](../experiment/report/benchmark_runs/af_branch_latency_20261105/latency_samples.csv), [thống kê và theo record](../experiment/report/benchmark_runs/af_branch_latency_20261105/latency_summary.json), [audit độc lập](../experiment/report/benchmark_runs/af_branch_latency_20261105/independent_audit.json). Lệnh chạy: `python experiment/src/experiments/af_detection/run_af_branch_latency.py --threads 2 --output <thư_mục_mới>`; thêm `--resume` để dùng lại tuyến đã hoàn tất và cùng protocol.

## Kết quả theo record trên cùng các hàng hợp lệ

| Record | Model | Số hàng chung | BA | Sensitivity | Specificity | F1 AF | AUC | Coverage |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 04936 | Logistic Regression RR | 3,263 | 90.53% | 87.39% | 93.67% | 92.06% | 0.9581 | 90.34% |
| 04936 | Random Forest RR | 3,263 | 88.70% | 90.07% | 87.34% | 92.37% | 0.9374 | 90.34% |
| 04936 | Extra Trees float RR | 3,263 | 91.26% | 92.12% | 90.39% | 94.06% | 0.9632 | 90.34% |
| 04936 | Extra Trees INT8 RR | 3,263 | 91.36% | 92.12% | 90.61% | 94.10% | 0.9620 | 90.34% |
| 04936 | XGBoost RR | 3,263 | 88.08% | 89.69% | 86.46% | 92.00% | 0.9333 | 90.34% |
| 04936 | ConvNeXtV2 FP32 | 3,263 | 76.99% | 86.62% | 67.36% | 86.90% | 0.8887 | 100.00% |
| 04936 | ECGFounder 1 lead, đầu gốc 150 nhãn | 3,263 | 89.46% | 96.72% | 82.21% | 94.98% | 0.9265 | 100.00% |
| 05091 | Logistic Regression RR | 3,166 | 99.54% | 100.00% | 99.08% | 12.12% | 0.9991 | 86.29% |
| 05091 | Random Forest RR | 3,166 | 99.86% | 100.00% | 99.72% | 30.77% | 1.0000 | 86.29% |
| 05091 | Extra Trees float RR | 3,166 | 99.84% | 100.00% | 99.68% | 28.57% | 1.0000 | 86.29% |
| 05091 | Extra Trees INT8 RR | 3,166 | 99.73% | 100.00% | 99.46% | 19.05% | 1.0000 | 86.29% |
| 05091 | XGBoost RR | 3,166 | 99.79% | 100.00% | 99.59% | 23.53% | 0.9998 | 86.29% |
| 05091 | ConvNeXtV2 FP32 | 3,166 | 97.68% | 100.00% | 95.35% | 2.65% | 0.9994 | 100.00% |
| 05091 | ECGFounder 1 lead, đầu gốc 150 nhãn | 3,166 | 99.57% | 100.00% | 99.15% | 12.90% | 0.9992 | 100.00% |
| 05261 | Logistic Regression RR | 3,146 | 97.40% | 100.00% | 94.79% | 30.17% | 0.9901 | 85.91% |
| 05261 | Random Forest RR | 3,146 | 97.81% | 100.00% | 95.63% | 33.98% | 0.9965 | 85.91% |
| 05261 | Extra Trees float RR | 3,146 | 95.64% | 100.00% | 91.29% | 20.53% | 0.9948 | 85.91% |
| 05261 | Extra Trees INT8 RR | 3,146 | 95.68% | 100.00% | 91.35% | 20.65% | 0.9953 | 85.91% |
| 05261 | XGBoost RR | 3,146 | 96.37% | 97.14% | 95.60% | 33.01% | 0.9969 | 85.91% |
| 05261 | ConvNeXtV2 FP32 | 3,146 | 74.21% | 94.29% | 54.13% | 4.41% | 0.8557 | 100.00% |
| 05261 | ECGFounder 1 lead, đầu gốc 150 nhãn | 3,146 | 97.16% | 100.00% | 94.31% | 28.34% | 0.9995 | 100.00% |
| 06453 | Logistic Regression RR | 2,254 | 79.95% | 60.71% | 99.19% | 53.97% | 0.9884 | 67.95% |
| 06453 | Random Forest RR | 2,254 | 81.81% | 64.29% | 99.33% | 59.02% | 0.9900 | 67.95% |
| 06453 | Extra Trees float RR | 2,254 | 86.85% | 75.00% | 98.70% | 53.85% | 0.9856 | 67.95% |
| 06453 | Extra Trees INT8 RR | 2,254 | 86.69% | 75.00% | 98.38% | 49.41% | 0.9860 | 67.95% |
| 06453 | XGBoost RR | 2,254 | 83.39% | 67.86% | 98.92% | 53.52% | 0.9862 | 67.95% |
| 06453 | ConvNeXtV2 FP32 | 2,254 | 52.22% | 7.14% | 97.30% | 4.44% | 0.8222 | 100.00% |
| 06453 | ECGFounder 1 lead, đầu gốc 150 nhãn | 2,254 | 89.50% | 82.14% | 96.86% | 38.02% | 0.9437 | 100.00% |
| 07859 | Logistic Regression RR | 3,360 | N/A: chỉ có lớp AF, không có mẫu non-AF | 80.74% | N/A: chỉ có lớp AF, không có mẫu non-AF | 89.35% | N/A: chỉ có lớp AF, không có mẫu non-AF | 91.28% |
| 07859 | Random Forest RR | 3,360 | N/A: chỉ có lớp AF, không có mẫu non-AF | 68.42% | N/A: chỉ có lớp AF, không có mẫu non-AF | 81.25% | N/A: chỉ có lớp AF, không có mẫu non-AF | 91.28% |
| 07859 | Extra Trees float RR | 3,360 | N/A: chỉ có lớp AF, không có mẫu non-AF | 86.04% | N/A: chỉ có lớp AF, không có mẫu non-AF | 92.50% | N/A: chỉ có lớp AF, không có mẫu non-AF | 91.28% |
| 07859 | Extra Trees INT8 RR | 3,360 | N/A: chỉ có lớp AF, không có mẫu non-AF | 85.68% | N/A: chỉ có lớp AF, không có mẫu non-AF | 92.29% | N/A: chỉ có lớp AF, không có mẫu non-AF | 91.28% |
| 07859 | XGBoost RR | 3,360 | N/A: chỉ có lớp AF, không có mẫu non-AF | 54.91% | N/A: chỉ có lớp AF, không có mẫu non-AF | 70.89% | N/A: chỉ có lớp AF, không có mẫu non-AF | 91.28% |
| 07859 | ConvNeXtV2 FP32 | 3,360 | N/A: chỉ có lớp AF, không có mẫu non-AF | 50.98% | N/A: chỉ có lớp AF, không có mẫu non-AF | 67.53% | N/A: chỉ có lớp AF, không có mẫu non-AF | 100.00% |
| 07859 | ECGFounder 1 lead, đầu gốc 150 nhãn | 3,360 | N/A: chỉ có lớp AF, không có mẫu non-AF | 96.07% | N/A: chỉ có lớp AF, không có mẫu non-AF | 98.00% | N/A: chỉ có lớp AF, không có mẫu non-AF | 100.00% |
| 08434 | Logistic Regression RR | 3,281 | 98.18% | 97.64% | 98.73% | 85.22% | 0.9973 | 89.28% |
| 08434 | Random Forest RR | 3,281 | 96.01% | 93.70% | 98.32% | 79.60% | 0.9954 | 89.28% |
| 08434 | Extra Trees float RR | 3,281 | 97.93% | 99.21% | 96.64% | 70.19% | 0.9945 | 89.28% |
| 08434 | Extra Trees INT8 RR | 3,281 | 97.53% | 99.21% | 95.85% | 65.62% | 0.9919 | 89.28% |
| 08434 | XGBoost RR | 3,281 | 96.22% | 96.06% | 96.39% | 67.22% | 0.9946 | 89.28% |
| 08434 | ConvNeXtV2 FP32 | 3,281 | 80.41% | 61.42% | 99.40% | 69.64% | 0.9766 | 100.00% |
| 08434 | ECGFounder 1 lead, đầu gốc 150 nhãn | 3,281 | 98.97% | 99.21% | 98.73% | 86.01% | 0.9985 | 100.00% |

## Kết quả riêng từng model trên các hàng có output

| Model | Số hàng hợp lệ | BA | Sensitivity | Specificity | F1 AF | AUC | Coverage |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Logistic Regression RR | 18,470 | 90.67% | 83.78% | 97.56% | 88.66% | 0.9817 | 85.45% |
| Random Forest RR | 18,470 | 87.57% | 77.76% | 97.38% | 84.83% | 0.9825 | 85.45% |
| Extra Trees float RR | 18,470 | 92.38% | 88.78% | 95.99% | 89.98% | 0.9807 | 85.45% |
| Extra Trees INT8 RR | 18,470 | 92.14% | 88.57% | 95.71% | 89.60% | 0.9786 | 85.45% |
| XGBoost RR | 18,470 | 83.34% | 69.96% | 96.72% | 79.08% | 0.9783 | 85.45% |
| ConvNeXtV2 FP32 | 21,616 | 75.31% | 65.53% | 85.10% | 65.51% | 0.8754 | 100.00% |
| ECGFounder 1 lead, đầu gốc 150 nhãn | 21,616 | 96.18% | 96.29% | 96.08% | 93.77% | 0.9913 | 100.00% |

### Theo record

| Record | Model | Số hàng hợp lệ | BA | Sensitivity | Specificity | F1 AF | AUC | Coverage |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 04936 | Logistic Regression RR | 3,263 | 90.53% | 87.39% | 93.67% | 92.06% | 0.9581 | 90.34% |
| 04936 | Random Forest RR | 3,263 | 88.70% | 90.07% | 87.34% | 92.37% | 0.9374 | 90.34% |
| 04936 | Extra Trees float RR | 3,263 | 91.26% | 92.12% | 90.39% | 94.06% | 0.9632 | 90.34% |
| 04936 | Extra Trees INT8 RR | 3,263 | 91.36% | 92.12% | 90.61% | 94.10% | 0.9620 | 90.34% |
| 04936 | XGBoost RR | 3,263 | 88.08% | 89.69% | 86.46% | 92.00% | 0.9333 | 90.34% |
| 04936 | ConvNeXtV2 FP32 | 3,612 | 76.79% | 86.92% | 66.67% | 87.13% | 0.8901 | 100.00% |
| 04936 | ECGFounder 1 lead, đầu gốc 150 nhãn | 3,612 | 89.34% | 96.57% | 82.12% | 94.99% | 0.9271 | 100.00% |
| 05091 | Logistic Regression RR | 3,166 | 99.54% | 100.00% | 99.08% | 12.12% | 0.9991 | 86.29% |
| 05091 | Random Forest RR | 3,166 | 99.86% | 100.00% | 99.72% | 30.77% | 1.0000 | 86.29% |
| 05091 | Extra Trees float RR | 3,166 | 99.84% | 100.00% | 99.68% | 28.57% | 1.0000 | 86.29% |
| 05091 | Extra Trees INT8 RR | 3,166 | 99.73% | 100.00% | 99.46% | 19.05% | 1.0000 | 86.29% |
| 05091 | XGBoost RR | 3,166 | 99.79% | 100.00% | 99.59% | 23.53% | 0.9998 | 86.29% |
| 05091 | ConvNeXtV2 FP32 | 3,669 | 84.94% | 75.00% | 94.87% | 3.08% | 0.9615 | 100.00% |
| 05091 | ECGFounder 1 lead, đầu gốc 150 nhãn | 3,669 | 99.35% | 100.00% | 98.69% | 14.29% | 0.9990 | 100.00% |
| 05261 | Logistic Regression RR | 3,146 | 97.40% | 100.00% | 94.79% | 30.17% | 0.9901 | 85.91% |
| 05261 | Random Forest RR | 3,146 | 97.81% | 100.00% | 95.63% | 33.98% | 0.9965 | 85.91% |
| 05261 | Extra Trees float RR | 3,146 | 95.64% | 100.00% | 91.29% | 20.53% | 0.9948 | 85.91% |
| 05261 | Extra Trees INT8 RR | 3,146 | 95.68% | 100.00% | 91.35% | 20.65% | 0.9953 | 85.91% |
| 05261 | XGBoost RR | 3,146 | 96.37% | 97.14% | 95.60% | 33.01% | 0.9969 | 85.91% |
| 05261 | ConvNeXtV2 FP32 | 3,662 | 73.62% | 92.31% | 54.93% | 4.22% | 0.8364 | 100.00% |
| 05261 | ECGFounder 1 lead, đầu gốc 150 nhãn | 3,662 | 96.91% | 100.00% | 93.82% | 25.83% | 0.9995 | 100.00% |
| 06453 | Logistic Regression RR | 2,254 | 79.95% | 60.71% | 99.19% | 53.97% | 0.9884 | 67.95% |
| 06453 | Random Forest RR | 2,254 | 81.81% | 64.29% | 99.33% | 59.02% | 0.9900 | 67.95% |
| 06453 | Extra Trees float RR | 2,254 | 86.85% | 75.00% | 98.70% | 53.85% | 0.9856 | 67.95% |
| 06453 | Extra Trees INT8 RR | 2,254 | 86.69% | 75.00% | 98.38% | 49.41% | 0.9860 | 67.95% |
| 06453 | XGBoost RR | 2,254 | 83.39% | 67.86% | 98.92% | 53.52% | 0.9862 | 67.95% |
| 06453 | ConvNeXtV2 FP32 | 3,317 | 51.97% | 6.25% | 97.69% | 3.64% | 0.8393 | 100.00% |
| 06453 | ECGFounder 1 lead, đầu gốc 150 nhãn | 3,317 | 90.71% | 84.38% | 97.05% | 34.62% | 0.9501 | 100.00% |
| 07859 | Logistic Regression RR | 3,360 | N/A: chỉ có lớp AF, không có mẫu non-AF | 80.74% | N/A: chỉ có lớp AF, không có mẫu non-AF | 89.35% | N/A: chỉ có lớp AF, không có mẫu non-AF | 91.28% |
| 07859 | Random Forest RR | 3,360 | N/A: chỉ có lớp AF, không có mẫu non-AF | 68.42% | N/A: chỉ có lớp AF, không có mẫu non-AF | 81.25% | N/A: chỉ có lớp AF, không có mẫu non-AF | 91.28% |
| 07859 | Extra Trees float RR | 3,360 | N/A: chỉ có lớp AF, không có mẫu non-AF | 86.04% | N/A: chỉ có lớp AF, không có mẫu non-AF | 92.50% | N/A: chỉ có lớp AF, không có mẫu non-AF | 91.28% |
| 07859 | Extra Trees INT8 RR | 3,360 | N/A: chỉ có lớp AF, không có mẫu non-AF | 85.68% | N/A: chỉ có lớp AF, không có mẫu non-AF | 92.29% | N/A: chỉ có lớp AF, không có mẫu non-AF | 91.28% |
| 07859 | XGBoost RR | 3,360 | N/A: chỉ có lớp AF, không có mẫu non-AF | 54.91% | N/A: chỉ có lớp AF, không có mẫu non-AF | 70.89% | N/A: chỉ có lớp AF, không có mẫu non-AF | 91.28% |
| 07859 | ConvNeXtV2 FP32 | 3,681 | N/A: chỉ có lớp AF, không có mẫu non-AF | 50.69% | N/A: chỉ có lớp AF, không có mẫu non-AF | 67.28% | N/A: chỉ có lớp AF, không có mẫu non-AF | 100.00% |
| 07859 | ECGFounder 1 lead, đầu gốc 150 nhãn | 3,681 | N/A: chỉ có lớp AF, không có mẫu non-AF | 96.03% | N/A: chỉ có lớp AF, không có mẫu non-AF | 97.98% | N/A: chỉ có lớp AF, không có mẫu non-AF | 100.00% |
| 08434 | Logistic Regression RR | 3,281 | 98.18% | 97.64% | 98.73% | 85.22% | 0.9973 | 89.28% |
| 08434 | Random Forest RR | 3,281 | 96.01% | 93.70% | 98.32% | 79.60% | 0.9954 | 89.28% |
| 08434 | Extra Trees float RR | 3,281 | 97.93% | 99.21% | 96.64% | 70.19% | 0.9945 | 89.28% |
| 08434 | Extra Trees INT8 RR | 3,281 | 97.53% | 99.21% | 95.85% | 65.62% | 0.9919 | 89.28% |
| 08434 | XGBoost RR | 3,281 | 96.22% | 96.06% | 96.39% | 67.22% | 0.9946 | 89.28% |
| 08434 | ConvNeXtV2 FP32 | 3,675 | 80.03% | 60.71% | 99.35% | 68.55% | 0.9766 | 100.00% |
| 08434 | ECGFounder 1 lead, đầu gốc 150 nhãn | 3,675 | 98.99% | 99.29% | 98.70% | 85.54% | 0.9985 | 100.00% |

## Kết luận từ lượt chạy

Trên cùng 18,470 hàng, **ECGFounder 1 lead, đầu gốc 150 nhãn** đạt BA cao nhất **96.28%**, F1 AF **94.26%**. Bảng riêng thể hiện ảnh hưởng của các hàng thiếu output RR tới cohort đánh giá.

Sáu record không thuộc phần train của bốn baseline cổ điển và model INT8 xuất từ Extra Trees; ConvNeXt được train/validation bằng LTAFDB và SHDB-AF. ConvNeXt ở đây là bản dự án tự triển khai với trọng số hiện có. Mức giao nhau của AFDB với pretraining ECGFounder chưa xác định. AFDB từng được dùng ở các đánh giá trước của dự án.

## Kiểm tra chấp nhận và dữ liệu để đối soát

- Đủ 21.740 hàng nguồn, 21.616 thuần nhãn; BA/AUC để trống khi record chỉ có một lớp.
- Mọi cửa sổ feature truy được về 31 peak liên tiếp/30 RR; score và lượng tử hóa INT8 khớp lời gọi C trực tiếp.
- Đối chiếu batch với chạy đơn đã qua trên 32 cửa sổ đầu của từng record.
- Hash dữ liệu, model và code không đổi trong lượt chạy hoàn chỉnh; confusion matrix và metric được tính lại từ CSV đã lưu.
- Có kiểm tra độc lập từ NPZ/CSV và metric đã xuất. Không thêm hoặc chạy unit test; không train lại hoặc chọn ngưỡng bằng tập đánh giá.
- Record đầu tái sử dụng cache sau lỗi xuất mốc RR `uint32`; cache được nhập có audit riêng, xác minh nguyên vẹn mọi mảng dự đoán/peak và phần code tạo cache. Bước RR được dựng lại hoàn toàn từ peak theo thứ tự.

Tệp xuất chính:

| Tệp | Nội dung |
| --- | --- |
| [protocol.json](../experiment/report/benchmark_runs/af_models_full_afdb_20261105_v2/protocol.json) | Giao thức, inventory và SHA256 dữ liệu/model/code |
| [base_models.json](../experiment/report/benchmark_runs/af_models_full_afdb_20261105_v2/base_models.json) | Artifact, đầu ECGFounder, preprocessing, dữ liệu train và cấu hình runtime |
| [predictions.csv](../experiment/report/benchmark_runs/af_models_full_afdb_20261105_v2/predictions.csv) | Từng khối, nhãn, feature RR, score/quyết định 7 model, lý do loại và cohort chung |
| [metrics.json](../experiment/report/benchmark_runs/af_models_full_afdb_20261105_v2/metrics.json) | Confusion matrix và metric gộp/theo record, chung/riêng |
| [audit.json](../experiment/report/benchmark_runs/af_models_full_afdb_20261105_v2/audit.json) | Kiểm tra chấp nhận tích hợp trong benchmark |
| [independent_audit.json](../experiment/report/benchmark_runs/af_models_full_afdb_20261105_v2/independent_audit.json) | Đối soát độc lập từ các tệp đã lưu |
| [cache_import.json](../experiment/report/benchmark_runs/af_models_full_afdb_20261105_v2/cache_import.json) | Provenance nhập lại cache sau sửa bước xuất RR |
| `RECORD/chunk_XXXXX.npz` | Mỗi 128 khối: 150 output ECGFounder, 2 output ConvNeXt, peak SWT, timing và checksum sidecar |
| `RECORD/rr_events.npz` | Timeline C, feature, lượng tử hóa, chỉ số 31 peak và 30 RR của từng cửa sổ |
| `RECORD/batch_single_check.json` | Sai lệch batch/đơn của hai mạng waveform |

## Chạy lại hoặc tiếp tục

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_af_model_comparison.py --threads 2 --resume --output experiment\report\benchmark_runs\af_models_full_afdb_20261105_v2
```

Để chạy mới, bỏ `--resume` và chọn `--output` chưa có protocol. Cache thiếu checksum được tính lại; cache có hash sai hoặc protocol khác sẽ bị từ chối. Giữ cùng phiên bản runtime khi tiếp tục; phiên bản lượt này nằm trong [environment.json](../experiment/report/benchmark_runs/af_models_full_afdb_20261105_v2/environment.json).

Mã Python: [runner](../experiment/src/experiments/af_detection/run_af_model_comparison.py), [RR classifiers](../experiment/src/experiments/af_detection/fusion_sync/rr_classifiers.py), [batch waveform](../experiment/src/experiments/af_detection/fusion_sync/comparison_waveforms.py), [metrics](../experiment/src/experiments/af_detection/fusion_sync/comparison_metrics.py).

Báo cáo tổng hợp AF: [report.md](../experiment/report/af_det/report.md). Các lượt SHDB-AF và đầu ECGFounder–LR lịch sử có giao thức riêng; số đo của chúng không được dùng trong bảng so sánh này.
