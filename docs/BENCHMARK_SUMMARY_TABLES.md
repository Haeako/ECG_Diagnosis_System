# Tổng kết benchmark mới nhất

Cập nhật 19/11/2026. Chỉ giữ các lượt chạy đủ cohort và đã đối soát.

## BPM

EDB: 57 record, lead 0; 22 điều kiện: Original; BW/MA/EM × 6 SNR (−6, 0, 6, 12, 18, 24 dB); BW/MA/EM mixed (mỗi burst trộn sáu mức). Accuracy là bản C chạy trên host, cửa sổ 10 giây, bước 5 giây. MAE/RMSE dưới đây là trung bình đều theo điều kiện; coverage là giá trị thấp nhất trong 22 điều kiện.

| Phương pháp C | MAE (BPM) | RMSE (BPM) | F1 R-peak ±50 ms % | F1 R-peak ±150 ms % | Coverage thấp nhất % | ESP32 p50 (ms/10s ECG) | Workspace (KiB) |
|---|---:|---:|---:|---:|---:|---:|---:|
| Fast-QRS | 0.764 | 3.212 | 43.90 | 98.65 | 99.998 | 13.230 | 63.67 |
| Pan–Tompkins++ | 1.032 | 3.988 | 80.82 | 98.87 | 99.990 | 443.380 | 90.24 |
| WIM Nguyen | 1.427 | 5.168 | 55.68 | 98.52 | 99.172 | 6.902 | 67.05 |
| WIM Hamilton | 1.367 | 5.223 | 43.79 | 98.68 | 99.295 | 6.003 | 55.02 |
| WIM Pan–Tompkins | 1.633 | 5.507 | 68.15 | 98.40 | 99.334 | 8.248 | 50.09 |
| WIM Elgendi | 1.708 | 5.724 | 59.91 | 97.37 | 99.982 | 9.850 | 60.14 |
| Elgendi | 2.024 | 5.857 | 1.08 | 96.47 | 100.000 | 6.258 | 14.93 |
| WIM Xu | 1.961 | 6.016 | 1.76 | 96.29 | 99.523 | 1506.672 | 112.59 |
| WIM Shaik | 2.318 | 6.485 | 70.18 | 97.57 | 100.000 | 252.981 | 121.77 |
| WIM Zhai | 2.138 | 7.020 | 53.66 | 98.28 | 99.694 | 85.899 | 57.37 |
| WIM Arteaga-Falconi | 2.179 | 8.039 | 83.45 | 97.87 | 99.912 | 22.986 | 67.36 |
| Pan–Tompkins | 4.301 | 12.877 | 15.44 | 86.85 | 99.785 | 42.962 | 53.19 |

ESP32-D0WD-V3, 160 MHz: 66 đoạn × 12 phương pháp, sai lệch BPM với Python tối đa 0,05734%. Timer chỉ đo detector, một call/đoạn; workspace không phải tổng RAM firmware.

F1 R-peak: ghép một-một trong ±50/150 ms, trung bình đều 22 điều kiện. Chỉ lấy đỉnh trong 5 giây mới của mỗi cửa sổ, chấm từ giây 5 để không đếm lặp. Chấm vị trí QRS do module trả về, không thêm bước hiệu chỉnh vị trí chung.

MAE/RMSE tính trực tiếp mọi sai số BPM hợp lệ, không áp ngưỡng bỏ lỗi. ±1% là tiêu chí parity C/ESP32 so với Python, không phải tolerance với BPM tham chiếu.

### BPM cho server — xử lý toàn record

Cùng EDB 57 record, lead 0, 22 điều kiện nhiễu. Các implementation host xử lý toàn record; BPM cập nhật mỗi 5 giây theo giao thức đã chạy. MAE/RMSE và F1 là trung bình đều 22 điều kiện; coverage là thấp nhất. Pan–Tompkins++ dùng bản sửa phát peak lặp. Khác bảng C/ESP32 phía trên ở trạng thái detector và cửa sổ xử lý; không so hai bảng như cùng một cấu hình.

| Phương pháp host | MAE (BPM) | RMSE (BPM) | F1 R-peak ±50 ms % | F1 R-peak ±150 ms % | Coverage thấp nhất % |
|---|---:|---:|---:|---:|---:|
| SWT-SepConv | 0.435 | 2.586 | 70.99 | 99.63 | 99.973 |
| Fast-QRS | 0.945 | 4.051 | 44.05 | 99.05 | 99.998 |
| Pan–Tompkins++ | 1.096 | 4.769 | 81.75 | 98.99 | 99.957 |
| WIM Hamilton | 1.468 | 5.460 | 43.51 | 98.53 | 96.257 |
| XQRS / WFDB | 1.340 | 5.502 | 69.34 | 98.66 | 96.235 |
| WIM Elgendi | 1.610 | 5.623 | 60.74 | 98.66 | 99.994 |
| Elgendi | 1.979 | 5.814 | 1.10 | 97.81 | 100.000 |
| WIM Shaik | 1.894 | 5.830 | 73.41 | 97.94 | 100.000 |
| WIM Nguyen | 1.823 | 6.590 | 55.41 | 98.38 | 97.326 |
| WIM Zhai | 1.928 | 6.785 | 53.39 | 98.47 | 97.507 |
| WIM Pan–Tompkins | 2.051 | 7.140 | 68.01 | 98.27 | 97.989 |
| WIM Xia | 2.232 | 7.536 | 7.22 | 98.21 | 96.586 |
| WIM Xu | 2.526 | 8.927 | 2.39 | 94.89 | 91.735 |
| Firmware C (baseline) | 2.416 | 9.016 | 62.04 | 97.64 | 100.000 |
| Pan–Tompkins | 2.762 | 10.246 | 8.43 | 84.94 | 86.950 |
| ECG2RR LSTM | 3.138 | 10.515 | 71.92 | 96.85 | 98.973 |
| GQRS / WFDB | 3.369 | 10.966 | 96.24 | 97.43 | 99.999 |
| WIM Arteaga-Falconi | 2.797 | 12.222 | 83.95 | 97.89 | 99.912 |

## SQI

BUTQDB: 35.773 cửa sổ 10 giây, 18 record / 15 người. AUC so trên cùng 30,978 cửa sổ hợp lệ ở cả chín phương pháp. Cohort chung loại nhiều cửa sổ xấu không tính được điểm ở bốn method mới; cột AUC mọi điểm hợp lệ giữ phạm vi riêng từng method. Latency host theo phạm vi đo ghi trong bảng.

| Phương pháp | AUC cùng cohort | AUC mọi điểm hợp lệ | Coverage % | Host p50 (ms) | Host p99 (ms) | Phạm vi latency |
|---|---:|---:|---:|---:|---:|---|
| Template matching | 0.9794 | 0.9792 | 86.62 | 19.969 | 44.317 | preprocess + R + SQI |
| Orphanidou external C | 0.9070 | 0.9154 | 100.00 | 12.379 | 15.074 | hai gate chung* |
| Ho | 0.8987 | 0.9792 | 100.00 | 11.763 | 15.853 | provider + kernel |
| bSQI C | 0.8953 | 0.9088 | 99.00 | 0.227 | 0.416 | XQRS + SQI* |
| Zhao shared-peak C | 0.8531 | 0.8073 | 100.00 | 12.379 | 15.074 | hai gate chung* |
| Dissimilarity | 0.8353 | 0.8331 | 86.62 | 19.494 | 43.088 | preprocess + R + SQI |
| Kramer C | 0.6999 | 0.9028 | 100.00 | 10.474 | 12.545 | native waveform |
| Zhao fuzzy | 0.5442 | 0.5554 | 86.77 | 9.252 | 20.069 | preprocess + R + SQI |
| averageQRS | 0.4836 | 0.4840 | 86.62 | 20.734 | 45.513 | preprocess + R + SQI |

* bSQI và hai gate Orphanidou/Zhao dùng primary R đã cache; timer hai gate đo chung, không phải latency riêng từng gate. Bốn SQI NeuroKit mới có coverage lớp tốt 100%, lớp xấu 16,68–17,64%. averageQRS là điểm tương đối trong cửa sổ; dissimilarity không có DTW. SQI chỉ là chỉ số phụ, không chặn AF.

## AF

AFDB: 6 record ECG1, 21.616 đoạn thuần nhãn 10 giây. So cả tám model trên cùng 18.470 đoạn hợp lệ (85,45%). Model/ngưỡng đóng băng, loại record trùng train cục bộ đã biết.

| Model | Sensitivity % | Specificity % | BA % | F1 AF % | AUC |
|---|---:|---:|---:|---:|---:|
| ECGFounder + Extra Trees INT8 RR + LR | 98.68 | 97.15 | 97.91 | 96.39 | 0.9896 |
| ECGFounder | 96.36 | 96.21 | 96.28 | 94.26 | 0.9913 |
| Extra Trees float RR | 88.78 | 95.99 | 92.38 | 89.98 | 0.9807 |
| Extra Trees INT8 RR | 88.57 | 95.71 | 92.14 | 89.60 | 0.9786 |
| Logistic Regression RR | 83.78 | 97.56 | 90.67 | 88.66 | 0.9817 |
| Random Forest RR | 77.76 | 97.38 | 87.57 | 84.83 | 0.9825 |
| XGBoost RR | 69.96 | 96.72 | 83.34 | 79.08 | 0.9783 |
| ConvNeXtV2 | 65.45 | 84.47 | 74.96 | 65.93 | 0.8705 |

ECGFounder: chưa xác minh trùng record với toàn bộ dữ liệu pretraining; không kết luận độc lập hoàn toàn với pretraining.

Fusion LR: train toàn bộ 93 người / 294 session SHDB đã chuẩn bị (5.292 cửa sổ; 5.275 đủ đầu vào), không phải toàn bộ thời lượng record gốc. Chọn ngưỡng 0,239196 bằng OOF 5-fold tách người trên SHDB, rồi fit lại toàn bộ; AFDB chỉ dùng đánh giá. Cùng đúng 18.470 cửa sổ và nhãn thuần của bảng trên. 18.302 cửa sổ chạy fusion, 167 fallback server và 1 fallback device; SQI không hard gate.
