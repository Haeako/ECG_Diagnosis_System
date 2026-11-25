# AFib: isolated latency + memory benchmark

Project này chỉ đo thời gian xử lý và bộ nhớ; input cố định trong chương trình.
Không có filter tiền xử lý (kể cả SB), BLE, acquisition, SD hoặc ghi file.
Kết quả chỉ in qua UART. Không flash/nạp thiết bị tự động.

Mọi source/header nằm trong main/components của project. Không dùng firmware
chung hoặc code của module khác. Helper invoke_idf.ps1 thuộc chính project.

```powershell
# Terminal tại experiment/AFib/ESP; lệnh này chỉ build trên máy tính
.\invoke_idf.ps1 -C . -B build_latency_memory build
```

BPM: chọn all hoặc một method bằng -DECG_BPM_ALGORITHM=fast_qrs.
SQI: chọn -DECG_BUILD_PROFILE=sqi-ho, sqi-xqrs, sqi-kramer,
sqi-orphanidou hoặc sqi-zhao. AFib: af-extra-trees.
Xem selected_methods.json của từng project.

Các tầng conditioning của port BPM đã bị loại bỏ theo yêu cầu; đây là biến thể
latency/memory, không phải implementation dùng cho bảng accuracy đã audit.
Các phép tính feature và quyết định đỉnh còn lại thuộc lõi module.
Workspace báo vùng thực cấp phát của cấu trúc hiện tại, không phải RAM tối thiểu
lý thuyết. Latency không gồm chuẩn bị input, cấp phát/reset workspace hoặc UART.
SQI/AFib giữ phạm vi kernel được in trong kết quả của từng method.

Nguồn gốc Git và checksum hiện tại ở source_manifest.json. Nguồn C gốc cho Python
accuracy được giữ riêng trong ../native/reference_main; firmware không link chúng.
Chưa chạy lại benchmark hoặc nạp thiết bị sau thay đổi.
