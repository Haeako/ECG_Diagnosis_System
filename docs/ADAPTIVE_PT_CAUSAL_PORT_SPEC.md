# Adaptive PT causal/ESP32 port readiness

## Quyết định phiên bản

Port ứng viên `adaptive_pt_v1`, không port v2 chỉ vì v2 mới hơn. V1 thắng v2 trên MIT-BIH external test về F1 và BPM MAE.

## Khoảng cách benchmark–firmware

Python v1 xử lý theo block và có phép nhìn tương lai như `sosfiltfilt` và median filter đối xứng, nên bản dịch trực tiếp không thể gọi là causal. Firmware hiện có detector sample-by-sample riêng trong `ExFeature/ExtractFeature.c` (notch, bandpass, two moving averages, adaptive threshold và căn chỉnh peak). Hai implementation liên quan nhưng chưa có parity.

## Contract cho bước port

1. Dùng SOS IIR causal và giữ state qua từng sample.
2. Baseline estimator chỉ nhìn quá khứ; ghi group delay từng stage.
3. Derivative, square và integration chạy bằng ring buffer.
4. Adaptive levels, refractory và search-back không dùng mẫu tương lai; peak alignment được phép tạo delay hữu hạn và phải báo cáo.
5. Tách timestamp R ước lượng khỏi thời điểm phát event để đo localization error và decision latency.
6. Kết quả stream phải độc lập với chunk size.

## Cổng nghiệm thu

- Host C và ESP32 chạy cùng golden vectors; peak count khớp trên vector sạch và peak lệch không quá 1 sample sau hiệu chỉnh delay cố định.
- Báo precision/recall/F1 ở 25/50/100/150 ms, BPM MAE, p50/p95 latency, stack, minimum free heap và flash tăng thêm.
- Chạy lại MIT-BIH frozen protocol và QTDB noise protocol mà không đổi threshold.
- Sau cùng mới chạy AD8232 thật; không tune trên dữ liệu đó rồi báo như external test.

Giao thức chọn phiên bản, external benchmark, tiêu chí causal và điểm tích hợp firmware đã rõ. Phần còn thiếu là implementation causal tương đương và golden-vector parity; chưa nên tuyên bố đã port xong.
