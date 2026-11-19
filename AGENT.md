# làm gì thì làm, các repo clone về phải được cô lập thành module , ko chỉnh sửa trực tiếp nó

## Phạm vi nhánh feature/experiment

- Nhánh này chỉ phục vụ chuẩn bị dataset, chạy module benchmark, tính metrics và tạo bảng/plot trong báo cáo BPM, SQI, AFib.
- Giữ cấu trúc ba domain `prepare_dataset.py`, `eval.py`, `metrics.py`, `modules/`; firmware chỉ là backend đo thử.
- Không đưa lại ứng dụng production, acquisition/BLE/web/gateway vào nhánh thực nghiệm.
- `eval.py --report` dùng cache đã audit và đối soát số đã chốt; suy luận mới hoặc train chỉ chạy khi được yêu cầu rõ ràng.

## Báo cáo benchmark ECG

- Giữ các repository clone bên thứ ba cô lập; không sửa trực tiếp source upstream.
- Báo cáo đọc chính duy nhất cho QRS/RR/BPM nằm tại `experiment/report/qrs_rr_bpm/REPORT.md` (thư mục theo bài toán, không theo ngày chạy).
- Khi thêm kết quả QRS/RR/BPM, hãy gộp bảng và phần giải thích vào báo cáo đọc chính này; không tạo báo cáo mới theo ngày. Thư mục mỗi lượt chạy chỉ giữ dữ liệu thô, protocol, hash và audit; file `REPORT.md` trong đó chỉ được trỏ về báo cáo chính.
- `REPORT_METRICS.md` chỉ tóm tắt và liên kết về báo cáo đọc chính.
- Không gọi latency Python/host là số đo ESP32. Ghi rõ dataset, lead, điều kiện nhiễu, cách tính BPM và audit khi thêm kết quả.
