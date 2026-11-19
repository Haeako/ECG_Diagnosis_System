# Tổng hợp dự án ECG

Cập nhật 23/10/2026. Gộp báo cáo tiến độ [`TONG_BAO_CAO_TIEN_DO_ECG.tex`](TONG_BAO_CAO_TIEN_DO_ECG.tex) với các lần chạy benchmark mới hơn.

## Hệ thống làm gì?

Thiết bị đọc ECG, tìm từng nhịp tim, tính BPM và đoán khả năng rung nhĩ (AF). RR là thời gian giữa hai nhịp. Mô hình AF đọc nhóm RR để phân loại tín hiệu. Thiết bị lưu dữ liệu vào SD và có thể gửi theo đợt qua BLE tới điện thoại/máy tính.

## Tìm nhịp tim và tính BPM

Đã thử trên 46 bản ghi MIT-BIH, 45 người, đạo trình MLII. Tất cả các phương pháp trong bảng nhận cùng tín hiệu và cùng nhãn nhịp; không thêm nhiễu nhân tạo. ±50 ms nghĩa là vị trí nhịp máy tìm được cách nhãn chuẩn không quá 50 ms. Điểm càng gần 1 càng tốt.

| Cách tìm nhịp | Độ nhạy | Độ chính xác dự đoán | F1 ±50 ms | F1 ±150 ms | Sai số RR | RR so sánh được | Sai số BPM |
|---|---:|---:|---:|---:|---:|---:|---:|
| Firmware hiện có: Pan-style, hai cửa sổ trung bình | 0.95623 | 0.96470 | 0.96045 | 0.98775 | 2.915 ms | 93.46% | 1.798 |
| WFDB XQRS | 0.98031 | 0.98145 | 0.98088 | 0.99256 | 2.941 ms | 96.21% | 0.761 |
| Pan–Tompkins, thư viện Python | 0.53530 | 0.53542 | 0.53536 | 0.98525 | 8.525 ms | 36.55% | 1.516 |
| Elgendi, thư viện Python | 0.15542 | 0.15308 | 0.15424 | 0.97760 | 1.754 ms | 10.69% | 1.654 |
| ECG2RR LSTM | 0.95731 | 0.98910 | 0.97294 | 0.97694 | 2.828 ms | 93.00% | 2.311 |
| FastQRS | 0.98453 | 0.99437 | 0.98942 | 0.99249 | 4.807 ms | 97.03% | 1.073 |
| Pan-Tompkins++ | 0.98665 | 0.80030 | 0.88376 | 0.89128 | 3.548 ms | 74.67% | 17.680 |
| SWT–SepConv | 0.99940 | 0.99710 | 0.99825 | 0.99849 | 1.902 ms | 99.88% | 0.098 |

Độ nhạy cho biết trong các nhịp chuẩn có bao nhiêu nhịp được tìm thấy. Độ chính xác dự đoán cho biết trong các nhịp máy báo, bao nhiêu nhịp khớp nhãn. F1 gộp hai số này. Sai số RR chỉ tính trên các cặp nhịp khớp; RR so sánh được cho biết bao nhiêu cặp nhịp chuẩn có thể đem ra so. BPM là số nhịp mỗi phút.

Pan–Tompkins và Elgendi có F1 thấp hơn khi yêu cầu vị trí chính xác ±50 ms, nhưng F1 tăng rõ khi cho phép ±150 ms. FastQRS có kết quả tốt trên bảng này. Pan-Tompkins++ tìm được nhiều nhịp nhưng cũng báo nhiều nhịp thừa, dẫn tới sai số BPM cao. SWT–SepConv có điểm cao nhất, nhưng model của nó đã được huấn luyện trên MIT-BIH nên con số này không phải thử nghiệm trên ECG hoàn toàn mới.

ECG2RR dùng model LSTM đi kèm thư viện, cửa sổ 250 mẫu, ngưỡng 0.05 và bỏ các nhịp cách nhau dưới 300 ms. Các phương pháp mới chạy trên MIT-BIH gốc; chưa chạy trên các đoạn thêm nhiễu. Chi tiết từng bản ghi nằm trong [báo cáo QRS/RR/BPM chính](qrs_rr_bpm/REPORT.md).

Đã đo thêm thời gian tính BPM bằng Python trên cùng 46 bản ghi: mỗi bản ghi lấy một đoạn 10 giây. So sánh 22 cách tìm nhịp, gồm detector của firmware đã port sang Python, các cách trong repo WIM và các model trong bảng trên. Kết quả latency và điều kiện chạy nằm trong [phần Python latency to BPM của báo cáo chính](qrs_rr_bpm/REPORT.md#python-latency-to-bpm); detector Python khớp chính xác timestamp với C trên cả 46 đoạn.

### SWT–SepConv trên QTDB — lần thử riêng

Trong lần thử QTDB cũ gồm 82 bản ghi, SWT–SepConv đạt precision 0.9978, recall 0.9990, F1 0.9984, BPM MAE 0.134. Model có 693,016 tham số, kích thước khoảng 2.9 MB. Đây là bộ dữ liệu khác với MIT-BIH ở bảng trên; chưa chạy trên ESP32.

### Thử khi thêm nhiễu

Trên thử nghiệm NSTDB với nhiễu trộn vào ECG, F1 firmware là:

| Mức nhiễu | −6 dB | 0 dB | 6 dB | 12 dB | 18 dB | 24 dB |
|---|---:|---:|---:|---:|---:|---:|
| Firmware | 0.3253 | 0.6892 | 0.9216 | 0.9797 | 1.0000 | 1.0000 |

Nhiễu càng mạnh thì tìm nhịp càng khó. Ở vùng −6 đến 6 dB cần báo tín hiệu kém tin cậy.

### Thời gian chạy trên ESP32

Đây là thời gian xử lý một đoạn tín hiệu dựng sẵn, không phải phép đo cả thiết bị khi đang đọc ECG thật.

| Thuật toán C chạy trên ESP32 | Đoạn tín hiệu | Thời gian trung vị | Lớn nhất ghi nhận | Bộ nhớ làm việc | Kích thước chương trình thử |
|---|---:|---:|---:|---:|---:|
| Pan–Tompkins | 3,600 mẫu, 10 giây, 360 Hz | 3.961 ms | 4.106 ms | 18,236 B | 157,712 B |
| Elgendi | 3,600 mẫu, 10 giây, 360 Hz | 7.776 ms | 7.887 ms | 15,284 B | 157,424 B |
| XQRS C-port | 3,500 mẫu, 14 giây, 250 Hz | 55.148 ms | 55.567 ms | 59,248 B | 155,520 B |

XQRS ở đây là bản C được chuyển từ WFDB, không phải chạy trực tiếp thư viện Python trên ESP32. Đã sửa lỗi thời gian nhịp lùi do phép tính số tràn và chạy lại benchmark. Firmware bình thường đã được nạp lại sau các lượt đo.

## Mô hình AF đọc RR (ECG → RR → AF)

Đầu vào của mô hình là 30 khoảng RR, cùng 5 số mô tả nhịp tim. Đã thử Logistic Regression, Random Forest, Extra Trees và XGBoost. BA là điểm cân bằng giữa nhận đúng AF và nhận đúng không-AF; F1 cân bằng giữa bỏ sót và báo nhầm; AUC đánh giá khả năng phân biệt khi thay đổi ngưỡng.

### AFDB: so sánh mô hình

| Mô hình | BA | Độ nhạy AF | Độ đặc hiệu | F1 | AUC |
|---|---:|---:|---:|---:|---:|
| Extra Trees | 0.9158 | 0.8676 | 0.9640 | 0.8942 | 0.9817 |
| Logistic Regression | 0.9022 | 0.8251 | 0.9794 | 0.8839 | 0.9831 |
| Random Forest | 0.8664 | 0.7591 | 0.9737 | 0.8378 | 0.9809 |
| XGBoost | 0.8244 | 0.6813 | 0.9676 | 0.7800 | 0.9765 |

Các số trên là thử nghiệm 20,194 cửa sổ, chia 18 bản ghi để xây dựng mô hình và 7 bản ghi để kiểm tra. Một lần chạy AF khác dùng 6 bản ghi có tín hiệu ECG đầy đủ để so RR từ nhãn với RR do firmware tìm. Không gộp số của hai lần chạy thành một bảng.

### RR chính xác và RR do firmware tìm

| Nguồn RR đưa vào cùng mô hình Extra Trees | BA | Độ nhạy AF | Độ đặc hiệu | F1 | AUC |
|---|---:|---:|---:|---:|---:|
| RR từ nhãn nhịp có sẵn | 0.9120 | 0.8672 | 0.9568 | 0.8951 | 0.9783 |
| RR do firmware tìm từ ECG | 0.8453 | 0.7787 | 0.9119 | 0.8073 | 0.9264 |
| Thay đổi | −0.0667 | −0.0885 | −0.0450 | −0.0878 | −0.0519 |

Kết quả cho thấy lỗi tìm nhịp làm giảm điểm F1 từ 0.8951 xuống 0.8073. Đây là lý do cần cải thiện bước ECG2RR, không chỉ chọn mô hình AF lớn hơn.

### Dùng cổng chất lượng trước khi chấp nhận kết quả AF

“Coverage” là tỷ lệ thời gian mà cổng cho phép dùng kết quả. “AF được giữ” là tỷ lệ thời gian AF không bị cổng loại bỏ. BA cao hơn có thể đi cùng việc bỏ nhiều dữ liệu hơn.

| Tín hiệu | Không có cổng: BA / coverage / AF giữ lại | Orphanidou: BA / coverage / AF giữ lại | Ho[NeuroKit] từ 80%: BA / coverage / AF giữ lại |
|---|---|---|---|
| ECG gốc | 0.8926 / 98.59% / 99.85% | 0.9122 / 85.75% / 74.03% | 0.8930 / 86.86% / 86.05% |
| Nhiễu cơ −6 dB | 0.8927 / 98.21% / 99.49% | 0.9125 / 85.05% / 72.15% | 0.8957 / 84.64% / 83.94% |
| Nhiễu điện cực −6 dB | 0.8682 / 91.28% / 87.83% | 0.8893 / 79.04% / 63.06% | 0.8727 / 78.23% / 73.38% |

Ho[NeuroKit] là cách báo chất lượng tín hiệu, không phải mô hình AF.

### Thử trên hai bộ dữ liệu AF khác

Các mô hình được xây dựng trên AFDB rồi chạy nguyên trạng trên dữ liệu khác, không huấn luyện lại.

| Bộ dữ liệu | Mô hình | BA | AUC | Coverage | AF giữ lại | AF phát hiện được trên toàn thời gian AF |
|---|---|---:|---:|---:|---:|---:|
| LTAFDB, 84 bản ghi | Logistic | 0.9184 | 0.9504 | 97.52% | 97.85% | 90.55% |
| LTAFDB | Random Forest | 0.9258 | 0.9663 | 97.52% | 97.85% | 93.05% |
| LTAFDB | Extra Trees | 0.9193 | 0.9567 | 97.52% | 97.85% | 95.04% |
| LTAFDB | Extra Trees INT8 | 0.9120 | 0.9545 | 97.52% | 97.85% | 95.24% |
| SHDB-AF, 98 bản ghi, 93 người | Logistic | 0.8956 | 0.9228 | 99.18% | 99.69% | 86.66% |
| SHDB-AF | Random Forest | 0.8953 | 0.9481 | 99.18% | 99.69% | 88.65% |
| SHDB-AF | Extra Trees | 0.8929 | 0.9418 | 99.18% | 99.69% | 89.97% |
| SHDB-AF | Extra Trees INT8 | 0.8871 | 0.9401 | 99.18% | 99.69% | 90.18% |

## Mô hình AF dạng INT8 trên ESP32

INT8 là cách lưu phép tính mô hình bằng số nguyên nhỏ để giảm dung lượng. Thời gian dưới đây chỉ tính bước mô hình; chưa tính ECG2RR và đọc ECG.

| Mô hình | Thời gian trung vị | Lớn nhất đo được | Dung lượng mô hình | RAM tạm | Kích thước chương trình thử |
|---|---:|---:|---:|---:|---:|
| Logistic Regression | 8.160 µs | 9.730 µs | 43 B | 17 B | 153,744 B |
| Random Forest | 607.320 µs | 620.620 µs | 215,833 B | 13 B | 368,928 B |
| Extra Trees | 391.580 µs | 405.550 µs | 141,881 B | 13 B | 294,416 B |

## SQI: báo tín hiệu tốt hay xấu

Trong lần thử 6,720 đoạn ECG, mô hình Extra Trees SQI đạt AUC 0.8117, BA 0.7881, cho qua 17.31% đoạn; trong số được cho qua, 19.69% vẫn là đoạn xấu. Mục tiêu đặt ra là dưới 5% đoạn xấu trong phần được nhận, nên mô hình này chưa được dùng làm cổng chính thức. Logistic với 3 chỉ số: coverage 6.24%, đoạn xấu trong phần được nhận 6.21% — cũng chưa đạt.

Các phép đo SQI trên ESP32:

| Cách tính | Thời gian xử lý | Bộ nhớ chính | Ghi chú |
|---|---:|---:|---|
| Orphanidou: luật và so dạng nhịp | 18.635 ms | 7,372 B + mẫu nhịp 4,320 B | Nhận nhịp từ bộ tìm nhịp khác. |
| Kramer: chỉ kiểm tra luật | Kết thúc: 0.681 µs; mỗi mẫu: tối đa 14.031 µs | 408 B | Nhánh nhẹ. |
| Kramer: xử lý toàn dạng sóng | 19.184 s | 60,700 B | Quá chậm cho xử lý trực tiếp. |
| Zhao-simple | 1.830 s | 14,532 B + vùng tạm 28,808 B | Quá chậm cho xử lý trực tiếp. |
| Ho[NeuroKit]: phần so khớp nhịp | 51.806 µs | Vùng tạm 4,112 B | Chưa gồm hai bộ tìm nhịp cần thiết. |
| Clifford: 5 chỉ số ECG | 119.612 ms | Vùng FFT 65,536 B | Chưa gồm toàn bộ các phần kết hợp của phương pháp. |

AD8232 chưa được nối, nên chưa đo được chất lượng tín hiệu từ mạch ECG thật.

## Việc còn lại

- Đo toàn bộ thiết bị khi chạy liên tục: đọc ECG, tìm nhịp, AF, ghi SD và gửi BLE.
- Đo pin, số mẫu bị bỏ lỡ và khả năng chạy lâu.
- Kiểm tra trên tín hiệu từ AD8232 khi mạch được nối.
- Chưa có đủ file ECG của BUT PPG 2.0 và VitalDB để chạy; chưa tải thêm dữ liệu.

## Báo cáo chi tiết

- [Báo cáo QRS/RR/BPM thống nhất: MIT-BIH, QTDB, nhiễu và latency](qrs_rr_bpm/REPORT.md)
- [AF và thử nhiễu/cổng chất lượng](benchmark_runs/af_timeline_20261021/REPORT_CURRENT.md)
- [AF trên LTAFDB và SHDB-AF](benchmark_runs/af_external_20261022/REPORT_CURRENT.md)
- [Thời gian và bộ nhớ QRS trên ESP32](benchmark_runs/device_bench_20261023_qrs/REPORT.md)
- [Thời gian và bộ nhớ SQI/AF trên ESP32](benchmark_runs/device_bench_20261023_sqi_af/REPORT.md)
- [Tổng hợp benchmark](benchmark_runs/REPORT_CURRENT.md)
- [Số liệu chi tiết 4 thuật toán mới](benchmark_runs/qrs_external_ecg2rr_20261023_corrected/metrics.csv) · [Kiểm tra số liệu](benchmark_runs/qrs_external_ecg2rr_20261023_corrected/audit.json)
