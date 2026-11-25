# Kết quả BPM

Tái tính từ cache đã audit; không suy luận, train hoặc đo lại thiết bị.

## python

Python/host accuracy: EDB 57 record, lead 0; 22 điều kiện; xử lý toàn record. MAE/RMSE và F1 trung bình đều 22 điều kiện; coverage lấy thấp nhất. Latency host lấy từ phép đo các đoạn 10s đã audit, không phải thời gian xử lý cả record. F1 ghép đỉnh một-một trong ±50/150 ms; MAE/RMSE không bỏ lỗi theo tolerance.

| Phương pháp | MAE (BPM) | RMSE (BPM) | F1 R-peak ±50 ms % | F1 R-peak ±150 ms % | Coverage thấp nhất % |
| :--- | ---: | ---: | ---: | ---: | ---: |
| SWT_SepConv_model | 0.4351 | 2.5862 | 70.9872 | 99.6289 | 99.9732 |
| FastQRS_0.3.0 | 0.9450 | 4.0509 | 44.0470 | 99.0461 | 99.9976 |
| PanTompkinsPlusPlus_2022 | 1.0962 | 4.7690 | 81.7496 | 98.9858 | 99.9573 |
| WIM_hamilton | 1.4683 | 5.4599 | 43.5106 | 98.5261 | 96.2569 |
| XQRS_WFDB | 1.3396 | 5.5021 | 69.3407 | 98.6637 | 96.2349 |
| WIM_elgendi | 1.6097 | 5.6226 | 60.7424 | 98.6642 | 99.9939 |
| Elgendi_pyecgdetectors | 1.9788 | 5.8142 | 1.0958 | 97.8085 | 100.0000 |
| WIM_shaik | 1.8939 | 5.8300 | 73.4093 | 97.9391 | 100.0000 |
| WIM_nguyen | 1.8225 | 6.5901 | 55.4131 | 98.3756 | 97.3262 |
| WIM_zhai | 1.9275 | 6.7852 | 53.3928 | 98.4681 | 97.5066 |
| WIM_pantompkins | 2.0505 | 7.1401 | 68.0148 | 98.2713 | 97.9894 |
| WIM_xia | 2.2322 | 7.5355 | 7.2211 | 98.2094 | 96.5861 |
| WIM_xu | 2.5257 | 8.9269 | 2.3852 | 94.8886 | 91.7346 |
| firmware_C | 2.4163 | 9.0161 | 62.0363 | 97.6431 | 100.0000 |
| PanTompkins_pyecgdetectors | 2.7623 | 10.2464 | 8.4259 | 84.9433 | 86.9503 |
| ECG2RR_LSTM | 3.1380 | 10.5148 | 71.9233 | 96.8464 | 98.9734 |
| GQRS_WFDB | 3.3690 | 10.9657 | 96.2414 | 97.4283 | 99.9988 |
| WIM_arteagaFalconi | 2.7974 | 12.2224 | 83.9497 | 97.8879 | 99.9122 |

### Latency trên máy tính

| Phương pháp | Host p50 (ms/10s ECG) |
| :--- | ---: |
| SWT_SepConv_model | 24.3245 |
| FastQRS_0.3.0 | 1.9889 |
| PanTompkinsPlusPlus_2022 | 1.8581 |
| WIM_hamilton | 1.1852 |
| XQRS_WFDB | 14.2095 |
| WIM_elgendi | 1.4284 |
| Elgendi_pyecgdetectors | 2.4816 |
| WIM_shaik | 1.2018 |
| WIM_nguyen | 2.2320 |
| WIM_zhai | 2.5890 |
| WIM_pantompkins | 2.3780 |
| WIM_xia | 14.6464 |
| WIM_xu | 1.2316 |
| firmware_C | 0.1781 |
| PanTompkins_pyecgdetectors | 3.2733 |
| ECG2RR_LSTM | 237.9302 |
| GQRS_WFDB | 18.8406 |
| WIM_arteagaFalconi | 1.5355 |

## esp32

C accuracy: EDB 57 record, lead 0; 22 điều kiện; context 10s/hop 5s, chỉ chấm QRS trong 5s mới, bắt đầu từ giây 5. MAE/RMSE và F1 trung bình đều 22 điều kiện; coverage lấy thấp nhất. ESP32 p50 là phép đo detector đã audit trên 66 đoạn 10s, ESP32 160 MHz; không gồm AFE, RR/BPM hoặc UART. Workspace là vùng làm việc thuật toán, không phải tổng RAM.

| Phương pháp | MAE (BPM) | RMSE (BPM) | F1 R-peak ±50 ms % | F1 R-peak ±150 ms % | Coverage thấp nhất % | ESP32 p50 (ms/10s ECG) | Workspace (KiB) |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| FastQRS_0.3.0 | 0.7645 | 3.2117 | 43.8979 | 98.6503 | 99.9976 | 13.2305 | 63.6680 |
| PanTompkinsPlusPlus_2022 | 1.0322 | 3.9880 | 80.8220 | 98.8722 | 99.9902 | 443.3795 | 90.2422 |
| WIM_nguyen | 1.4275 | 5.1684 | 55.6829 | 98.5244 | 99.1721 | 6.9025 | 67.0469 |
| WIM_hamilton | 1.3675 | 5.2228 | 43.7931 | 98.6791 | 99.2953 | 6.0030 | 55.0234 |
| WIM_pantompkins | 1.6333 | 5.5070 | 68.1483 | 98.3979 | 99.3343 | 8.2485 | 50.0938 |
| WIM_elgendi | 1.7075 | 5.7241 | 59.9122 | 97.3652 | 99.9817 | 9.8495 | 60.1445 |
| Elgendi_pyecgdetectors | 2.0237 | 5.8570 | 1.0806 | 96.4681 | 100.0000 | 6.2585 | 14.9258 |
| WIM_xu | 1.9613 | 6.0159 | 1.7611 | 96.2861 | 99.5233 | 1506.6725 | 112.5938 |
| WIM_shaik | 2.3184 | 6.4847 | 70.1801 | 97.5695 | 100.0000 | 252.9810 | 121.7656 |
| WIM_zhai | 2.1384 | 7.0204 | 53.6589 | 98.2845 | 99.6940 | 85.8985 | 57.3672 |
| WIM_arteagaFalconi | 2.1793 | 8.0387 | 83.4522 | 97.8725 | 99.9122 | 22.9860 | 67.3594 |
| PanTompkins_pyecgdetectors | 4.3012 | 12.8772 | 15.4424 | 86.8538 | 99.7854 | 42.9615 | 53.1875 |

