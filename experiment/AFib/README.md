# AFib

```powershell
# Từ thư mục gốc repository
experiment/AFib/.venv/Scripts/python.exe experiment/AFib/reproduce.py
```

Tám model trên cùng 18.470 cửa sổ AFDB thuần nhãn.
Model/ngưỡng đóng băng; không train/tune AFDB. Fusion giữ model SHDB đã fit.

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
