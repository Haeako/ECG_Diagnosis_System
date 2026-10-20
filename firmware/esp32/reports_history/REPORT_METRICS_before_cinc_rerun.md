# Báo cáo số liệu ECG prototype

Ngày tổng hợp: 19/10/2026. Các số dưới đây lấy từ artifact đang có trong repo.
Dấu `—` nghĩa là metric không áp dụng hoặc chưa được đo, không phải bằng 0.

## 1. Trạng thái build và codebase

| Hạng mục | Giá trị |
|---|---:|
| Target | ESP32-D0WD-V3 |
| CPU | 160 MHz |
| Flash image header | 4 MB |
| `ECG.bin` | 713.904 byte |
| C/H source files trong `main` | 77 |
| Tổng số dòng C/H gần đúng | 24.926 |
| ESP-IDF đã dùng | 6.0.1 |

## 2. QRS detector — QTDB MLII

Protocol: 13 QTDB records có MLII và annotation `atr`; resample 250→360 Hz;
one-to-one matching ±50 ms.

| Metric | Giá trị |
|---|---:|
| True positive | 14.108 |
| False positive | 19 |
| False negative | 48 |
| Sensitivity | 99,6609% |
| PPV | 99,8655% |
| F1 | 99,7631% |
| Event ACC `TP/(TP+FP+FN)` | 99,5273% |

## 3. SQI internal holdout

Protocol: cửa sổ 10 giây; 10.498 development và 6.720 held-out test;
nhãn production-RR usability, không phải expert acceptability.

| Method | AUC | Balanced accuracy | Coverage | Bad accepted |
|---|---:|---:|---:|---:|
| Orphanidou | 0,7637 | 0,7103 | 74,21% | 28,29% |
| bSQI ≥0,8 | **0,8235** | 0,6595 | **86,56%** | 33,13% |
| Zhao-simple | 0,7480 | 0,7029 | 77,31% | 29,32% |
| Kramer | 0,6511 | 0,6511 | 84,91% | 33,44% |
| Logistic-5SQI | 0,8007 | 0,5370 | 4,99% | **5,97%** |

## 4. SQI fusion trên internal holdout

| Method | AUC | Balanced accuracy | Coverage | Bad accepted |
|---|---:|---:|---:|---:|
| bSQI + Logistic score | 0,8069 | 0,7709 | 67,44% | 22,53% |
| bSQI + Orphanidou | 0,8057 | 0,7624 | 68,93% | 23,55% |
| bSQI + Zhao fusion | 0,8361 | 0,8086 | 71,56% | 21,09% |
| bSQI → Zhao cascade | 0,8184 | **0,8119** | 71,06% | 20,71% |
| Clifford-5 Logistic | 0,8263 | 0,7765 | 62,75% | 20,63% |
| Clifford-5 RBF-SVM | **0,8746** | 0,7881 | 65,89% | 20,80% |
| b+p+k+bas Logistic | 0,8387 | 0,7802 | 58,84% | **18,89%** |
| b+p+k+bas RBF-SVM | 0,8578 | 0,7955 | **71,88%** | 22,07% |

`bSQI→Zhao` đã bị loại khỏi shortlist production vì coverage và worst-case
latency. Nó được giữ làm đối chứng nghiên cứu.

## 5. CinC 2011 expert-label reproduction

Protocol: Set A; 998 binary-labelled records, gồm 773 acceptable và 225
unacceptable; record 10 giây; 5-fold stratified OOF.

| Method | Lead scope | BA | AUC | Coverage | Bad accepted |
|---|---|---:|---:|---:|---:|
| All-84 RBF-SVM | 12 lead | **0,9031** | **0,9535** | 74,45% | **3,63%** |
| All-84 Logistic | 12 lead | 0,8996 | 0,9510 | 74,35% | 3,77% |
| All-6 RBF-SVM | Lead II | **0,7439** | 0,7769 | 82,67% | 12,24% |
| All-6 Logistic | Lead II | 0,7284 | 0,7577 | 74,05% | 11,77% |
| Zhao-4 Logistic | Lead II | 0,7269 | 0,7552 | 75,15% | 12,00% |
| Zhao-4 RBF-SVM | Lead II | 0,7263 | 0,7728 | 82,16% | 12,93% |
| Zhao-simple fixed | Lead II | 0,5714 | 0,6775 | 83,07% | 19,54% |
| bSQI fixed ≥0,8 | Lead II | 0,4967 | 0,3973 | 67,94% | 22,71% |

All-84 là reference multilead, không phải candidate cho thiết bị single-lead.

## 6. ESP32 SQI cost

| Method/path | p99 push | Finalize | Flash delta | RAM delta |
|---|---:|---:|---:|---:|
| Orphanidou/template | 0,3875 µs | 18,939 ms | 2.320 B | 11.672 B |
| bSQI: primary + XQRS | 7,55 µs | 55,649 ms | 4.064 B | 59.512 B |
| Zhao-simple waveform | 0,1938 µs | 1.864,814 ms | 5.888 B | 15.528 B |
| Kramer full | 14,319 µs | 19.184,233 ms | 10.752 B | 60.704 B |
| Logistic-5SQI full | 7,55 µs | 458,328 ms | 12.448 B | 69.392 B |
| bSQI→Zhao cascade | — | 97,552 ms avg; 1.920,463 ms worst | — | — |

## 7. AFDB end-to-end production runtime

Protocol: sáu AFDB waveform records → production C QRS → production C AF;
17.932 pure-rhythm windows.

| Metric | Raw AF | Confirmed 3-of-4 |
|---|---:|---:|
| Balanced accuracy | 0,8878 | 0,8824 |
| AUC | 0,9720 | 0,9720 |
| TN | 10.531 | 10.698 |
| FP | 558 | 391 |
| FN | 1.191 | 1.368 |
| TP | 5.652 | 5.475 |

Confirmation giảm 167 false-positive windows nhưng tăng 177 false-negative
windows. Đây là trade-off cảnh báo, không phải cải thiện đồng thời mọi metric.

## 8. Ho-style gate + AF classifiers trên AFDB sạch

| SQI gate | AF model | Coverage | AF BA | Sensitivity | Specificity | AUC |
|---|---|---:|---:|---:|---:|---:|
| Không gate | Logistic | 100,00% | 0,8888 | 0,8047 | 0,9730 | 0,9802 |
| Ho ≥80% | Logistic | 96,16% | 0,8881 | 0,8026 | 0,9735 | 0,9804 |
| Không gate | Random Forest | 100,00% | 0,8488 | 0,7299 | 0,9678 | 0,9756 |
| Ho ≥80% | Random Forest | 96,16% | 0,8473 | 0,7270 | 0,9675 | 0,9751 |
| Không gate | Extra Trees | 100,00% | **0,8915** | 0,8293 | 0,9537 | 0,9747 |
| Ho ≥80% | Extra Trees | 96,16% | 0,8904 | 0,8273 | 0,9536 | 0,9744 |
| Ho ≥90% | Extra Trees | 89,34% | 0,8894 | 0,8221 | 0,9566 | 0,9755 |

Ho-style là RR-quality gate, không phải mô hình phân loại AF.

## 9. AFDB + NSTDB muscle-artifact burst

Noise `ma` channel 0; burst 10 giây mỗi 30 giây; Extra Trees frozen.

| SNR burst | Gate | Coverage | AF BA | Sensitivity | Specificity | AUC |
|---:|---|---:|---:|---:|---:|---:|
| Clean | Không gate | 100,00% | 0,8915 | 0,8293 | 0,9537 | 0,9747 |
| Clean | Ho ≥80% | 96,16% | 0,8904 | 0,8273 | 0,9536 | 0,9744 |
| 12 dB | Không gate | 100,00% | 0,8916 | 0,8299 | 0,9532 | 0,9744 |
| 12 dB | Ho ≥80% | 95,32% | 0,8904 | 0,8278 | 0,9531 | 0,9742 |
| 6 dB | Không gate | 100,00% | 0,8921 | 0,8336 | 0,9506 | 0,9736 |
| 6 dB | Ho ≥80% | 93,01% | 0,8905 | 0,8305 | 0,9505 | 0,9734 |
| 0 dB | Không gate | 100,00% | 0,8946 | 0,8460 | 0,9432 | 0,9692 |
| 0 dB | Ho ≥80% | 85,66% | 0,8928 | 0,8390 | 0,9467 | 0,9706 |

## 10. Hardware pipeline profiling

Capture khoảng 80 giây, 27.975 timer ticks/samples.

| Metric | Giá trị |
|---|---:|
| Effective sampling rate | 361,01 Hz |
| Processed/timer ticks | 27.975/27.975 |
| ISR jitter p99 absolute | ≤10 µs |
| ISR-to-task dispatch p99 | ≤25 µs |
| Pipeline p95/p99 | ≤250/≤250 µs |
| Pipeline observed max | 3.337 µs |
| Nominal period | 2.770 µs |
| SD write observed max | 47,179 ms |
| SD queue peak | 50,2% |
| BLE queue peak | 3,1% |
| Timer overruns | 0 |
| SD drops/errors | 0/0 |
| BLE queue drops | 0 |
| CPU0 mean | 19,8% |
| CPU1 mean | 7,3% |
| State task free stack | 664 B |
| ECG task free stack | 2.968 B |
| SD task free stack | 3.020 B |
| BLE TX free stack | 2.496 B |

Pipeline đạt throughput trong capture nhưng chưa đạt bằng chứng hard deadline
vì maximum 3.337 µs vượt period 2.770 µs.

## 11. SD/BLE hardware evidence

| Hạng mục | Giá trị |
|---|---:|
| SD card | SDHC 3.810 MB |
| SD clock | 20 MHz |
| Sustained SD smoke test | >65 giây |
| BLE capture duration | khoảng 30 giây |
| BLE waveform samples | 2.695 |
| BLE batch packets | 539 |
| BLE events | 8 |
| Sequence gaps | 0 |
| BAD-SQI events | 8/8 |
| AF valid events | 0 |

AD8232 chưa kết nối trong capture, vì vậy BAD SQI và AF invalid là kết quả
fail-safe đúng, không phải bằng chứng về hiệu năng ECG người thật.

## 12. Dữ liệu SD hiện có

Tại thời điểm tổng hợp, `D:\DOC\ECG_Diagnosis_System\data\record` có:

| Metric | Giá trị |
|---|---:|
| CSV files | 22 |
| Non-empty | 22 |
| Empty | 0 |
| Total size | 46.314.064 byte |

Case `D7AE5000.CSV`: các cửa sổ hoàn chỉnh đạt SQI 80–83, template
correlation 0,797–0,833 và rule mask 15. False BAD ban đầu đến từ cửa sổ
startup chỉ có 3.561 mẫu so với điều kiện tối thiểu 3.599 mẫu.

## 13. Trạng thái test gần nhất

| Test | Kết quả |
|---|---|
| AF runtime behavior | PASS |
| SQI clean/flat behavior | PASS |
| bSQI host test | PASS |
| Kramer rule host test | PASS |
| AF INT8 golden vectors | 512/512 exact score parity |
| RR-feature/model randomized parity | 1.000/1.000 |
| XQRS C parity | Exact peak indices trên 8 cửa sổ |
| Zhao-simple C parity | Decision parity; max feature error 3,9e-6 |
| Logistic-5SQI C parity | Max error 4,98e-7 |

## 14. Kết luận định lượng

1. Orphanidou/template có chi phí thấp nhất và phù hợp shadow/fallback.
2. Clifford-5 RBF-SVM tốt nhất theo AUC nghiên cứu nhưng chưa phù hợp ESP32.
3. Ho-style ≥80% giữ 96,16% AFDB sạch với suy hao AF BA khoảng 0,0011.
4. bSQI→Zhao không còn là candidate production.
5. Extra Trees INT8 đạt AF raw BA 0,8878/AUC 0,9720 trên replay production.
6. Firmware xử lý đủ throughput, nhưng còn latency outlier và stack margin.
7. Chưa có bằng chứng clinical validation hoặc live-electrode validation.

## 15. Nguồn artifact

- `README.md`
- `../../outputs/af_detection/firmware_rr/REPORT.md`
- `../../outputs/af_detection/firmware_rr/screening_metrics.json`
- `../../outputs/af_detection/int8_deployment/REPORT.md`
- `../../outputs/af_detection/int8_deployment/metrics.json`
- `../../outputs/ecg_quality_clifford_rahman_v1/`
- `../../outputs/clifford_rahman_sqi_v1/metrics.json`
- `../../reports/csv/af_detection_results.csv`
- `../../reports/csv/bpm_qrs_results.csv`
- `../../reports/csv/sqi_results.csv`

Các bảng CinC reproduction, Ho-style downstream, ESP32 linked-cost và hardware
capture trong file này là snapshot từ bộ `benchmark_results` đã được đọc và
kiểm tra ngày 18–19/10/2026. Thư mục đó không còn hiện diện trong checkout tại
thời điểm hoàn tất tài liệu này; cần khôi phục hoặc chạy lại trước khi dùng làm
artifact phát hành chính thức.
