# Kết quả firmware trên QTDB MLII — 13/09/2026

Theo yêu cầu chỉ lấy MLII, benchmark chọn nhãn MLII trong header, không
đồng nhất với kênh 0. Có 13 bản ghi đủ điều kiện; `sel114` dùng kênh 1.
Danh sách chọn và lý do loại 92 bản ghi còn lại nằm trong
`outputs/firmware_qtdb_mlii_verified/selection.json`.

Chạy trực tiếp mã C production `ecg_signal.c` và `SBfilter.c` trên host:

| Chỉ số gộp | Kết quả |
|---|---:|
| TP / FP / FN | 14.108 / 19 / 48 |
| Se | 99,6609% |
| PPV | 99,8655% |
| F1 | 99,7631% |
| ACC sự kiện = TP/(TP+FP+FN) | 99,5273% |

Chạy lại độc lập cho cùng kết quả. Đây là chỉ số gộp, không có nghĩa mọi
bản ghi đều đạt 99%. `sel213` có F1 98,9942%; `sel223` có Se 98,5485%.

## Cấu hình và bằng chứng

- Tín hiệu gốc QTDB 250 Hz được tái lấy mẫu về 360 Hz; không thêm nhiễu.
- Mô phỏng ghép AC bậc một 0,5 Hz nhân quả, khởi tạo theo mẫu đầu tiên;
  cố định 200 ADC counts/mV, tâm 2048, giới hạn 12 bit. Không mẫu nào bị
  cắt biên trong 13 bản ghi này. Đây chưa phải đặc tính AFE đã đo.
- Chạy liên tục 900 giây/bản ghi, không reset mỗi đoạn 10 giây.
- Giữ nguyên thời điểm `.atr`, ghép một-một ±50 ms; không dịch nhãn.
- `summary.json` lưu hash mã nguồn và 39 tệp đầu vào `.hea/.dat/.atr`.
- `per_record.csv` lưu đầy đủ từng bản ghi; `.npz` lưu đỉnh tham chiếu và
  đỉnh phát hiện để kiểm tra lại.
- Năm kiểm thử ghép đỉnh đạt, gồm trùng đỉnh, ranh giới dung sai, đỉnh
  ngoài dung sai, tham chiếu rỗng và trường hợp nearest-first mất cặp.
- ESP-IDF 6.0.1 build thành công, tạo `firmware/esp32/build/ECG.bin`
  (0x87800 byte; phân vùng ứng dụng còn trống 64%). Nhật ký:
  `outputs/firmware_qtdb_mlii/idf_build.log`.

## Thay đổi mã

Năng lượng dùng bình phương toàn tín hiệu thay vì chỉ nửa sóng dương.
Căn thời điểm QRS theo cực trị có độ lệch lớn nhất so với nền cục bộ,
ước lượng nền từ lịch sử tín hiệu. Điều này hỗ trợ QRS đảo cực.
Giữ nguyên cấu trúc streaming, bộ lọc và ngưỡng thời gian hiện có.

Sửa lỗi build trong `main.c`: chỉ giữ `IRAM_ATTR` tại định nghĩa hàm
ngắt SD, bỏ bản lặp tại khai báo trước để tránh xung đột section.

## Giới hạn và phần còn lại

Đây là benchmark phát triển trên MLII, không phải kiểm định độc lập theo
bệnh nhân hay đo trực tiếp trên ESP32. Chưa flash thiết bị, chưa đo tải CPU,
mất mẫu ADC, độ trễ BLE/SD hoặc tác động AFE thực tế.

Độ phủ nhãn đã được kiểm tra cấu trúc: 39/39 tệp khớp SHA256SUMS của
PhysioNet đã lưu, đủ 14.156 nhịp tham chiếu, không cửa sổ 10 giây nào
trống nhãn; khoảng đầu/cuối chưa có nhãn đều dưới 0,9 giây. Có 28 RR dài
hơn 3 giây, tất cả thuộc `sel232` (dài nhất 5,872 giây); giữ nguyên trong
phép chấm, chưa kết luận đó là lỗi nhãn hay khoảng ngừng nhịp chỉ bằng
kiểm tra cấu trúc. Chi tiết tại `annotation_coverage.csv` và
`annotation_audit.json` trong thư mục kết quả verified. Kiểm tra này không
thay thế đối chiếu chuyên môn từng nhịp; không lấy kết quả để khẳng định
toàn bộ QTDB hoặc ECG wearable đạt cùng hiệu năng.

Phần 2 của yêu cầu crawl bị cắt sau `[$hotline`; đang chờ tên dữ liệu/tài liệu
và nguồn tải. Chưa thể xác nhận toàn bộ mục tiêu hoàn tất.
