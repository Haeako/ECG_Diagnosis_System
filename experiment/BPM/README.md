# BPM

```powershell
# Từ thư mục gốc repository
experiment/BPM/.venv/Scripts/python.exe experiment/BPM/reproduce.py
```

Mặc định chạy cả hai backend; `--backend host` chỉ Python, `--backend c` chỉ C/ESP32.
Accuracy dùng EDB 57 record/22 điều kiện. MAE/RMSE và F1 là trung bình đều theo
điều kiện; coverage là thấp nhất. Latency host và ESP32 giữ phạm vi đo riêng.

- `reproduce.py`: đọc cache đã audit, tái tính metrics, đối soát reference và xuất bảng/plot.
- `preprocessing.py`: chuẩn bị manifest; helper tiền xử lý ở `modules/_runtime.py` hoặc trong từng method.
- `metrics.py`: định nghĩa metric.
- `modules/`: một adapter cho mỗi method; file `_...` là util nội bộ.
- `native/` (nếu có): cầu nối C phục vụ inference trên máy tính.
- `ESP/`: dự án và workspace ESP-IDF riêng, danh sách port tại `selected_methods.json`.
- `results/`: đầu ra mỗi lượt; `RESULTS.md` là bản đọc mới nhất.
- `.venv/`, `requirements.txt`, `installed.lock.txt`: môi trường riêng của mô đun.

Mặc định không train hoặc suy luận mới. Xem [hướng dẫn chung](../README.md).

## Isolation

`utils/` là bản util riêng của module. Các bridge C dùng `ESP/main/components/`
của chính module này. Project ESP có main/kernel/header/helper build cục bộ;
không tham chiếu firmware chung hoặc source của module khác.
Xem ESP/README.md để build bằng thư mục build_isolated mới.

## Firmware latency/memory only

ESP chỉ đo latency + memory với input cố định; không có filter, BLE hoặc ghi file.
Các biến thể BPM bỏ conditioning không dùng để đối chiếu accuracy cũ.
Nguồn C gốc của Python nằm tại native/reference_main, tách biệt với ESP.
