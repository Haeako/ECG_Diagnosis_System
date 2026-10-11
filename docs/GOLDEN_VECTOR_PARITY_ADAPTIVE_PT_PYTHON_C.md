# Golden-vector parity Adaptive PT giữa Python và C

Ngày kiểm tra: **09/10/2026**

## Kết luận

Implementation host C của `AdaptivePTPortable` đã đạt parity với Python trên toàn bộ bộ golden vectors:

- số peak giống nhau;
- từng timestamp peak lệch **0 sample**;
- event trace giống nhau tại từng sample;
- envelope, signal level (SPKI) và noise level (NPKI) giống nhau;
- cả 6/6 vectors đều pass.

Mục checklist **“Có golden vectors Python–C”** được xem là hoàn thành ở cấp host reference.

Điều này chưa đồng nghĩa đã nghiệm thu firmware ESP32. Sau host reference `double`, một bản firmware-oriented dùng `int16_t` cho input/raw history và `float32` cho DSP state cũng đã được kiểm tra riêng.

## Kết quả int16/float32

Bản firmware-oriented dùng:

- input ECG: `int16_t`;
- raw-history relocation: `int16_t[40]`;
- SOS/filter state: `float32`;
- energy ring: `float32[40]`;
- SPKI/NPKI/envelope: `float32`;
- index/timestamp: integer 32 bit.

Tất cả input dùng cùng gain cố định **200 count/đơn vị**, giống host adapter hiện tại; không normalize riêng từng record.

| Vector | Python peaks | int16 C peaks | Peak lệch lớn nhất | Kết quả |
|---|---:|---:|---:|---|
| Synthetic upright | 19 | 19 | 0 | Pass |
| Synthetic inverted + noise | 19 | 19 | 0 | Pass |
| QTDB `sel100` | 78 | 78 | 0 | Pass |
| QTDB `sel123` | 48 | 48 | 0 | Pass |
| NSTDB `118e00` | 104 | 104 | 0 | Pass |
| NSTDB `118e_6` | 130 | 130 | 0 | Pass |

State struct giảm từ **880 byte** ở bản `double` xuống **308 byte** ở bản int16/float32.

Compile thử bằng `xtensa-esp32-elf-gcc -Os`:

- function text: 898 byte;
- literal: 44 byte;
- object không có static `.bss`/`.data`;
- khi tích hợp streaming, 308 byte detector state cần được đặt persistent trong BSS hoặc component context, không đặt trên stack mỗi lần gọi.

## Benchmark đầy đủ sau lượng tử hóa

### QTDB locked test

| Detector | F1 @50 ms | F1 @100 ms | BPM MAE | Host latency/60 s |
|---|---:|---:|---:|---:|
| Adaptive PT v2 offline | **0,8896** | **0,9944** | **0,208** | 3,864 ms |
| Firmware hiện tại | 0,8446 | 0,9801 | 0,953 | 2,225 ms |
| Adaptive portable int16 | 0,8078 | 0,9386 | 5,074 | **0,799 ms** |

int16 C rất nhẹ và nhanh, nhưng chưa đạt cổng chất lượng để thay firmware hiện tại trên QTDB.

### NSTDB aggregate tại ±50 ms

| Detector | F1 | BPM MAE | Host latency/60 s |
|---|---:|---:|---:|
| Adaptive PT v2 offline | **0,8889** | **1,582** | 5,737 ms |
| Adaptive portable int16 | **0,8696** | 6,153 | **0,477 ms** |
| Firmware hiện tại | 0,8626 | 3,854 | 2,213 ms |

Ở riêng cửa sổ có nhiễu, portable int16 đạt F1 0,4649 tại −6 dB, 0,7295 tại 0 dB và 0,9864 tại 6 dB; tốt hơn firmware tương ứng 0,3253, 0,6892 và 0,9216. Điều này chưa bù được BPM MAE và kết quả QTDB yếu hơn.

Quyết định: giữ implementation int16 làm nền port/optimization, **chưa tích hợp làm detector production**.

## Tiêu chí nghiệm thu

| Điều kiện | Ngưỡng |
|---|---:|
| Peak count | Phải bằng nhau |
| Sai lệch từng peak | Không quá 1 sample |
| Event trace | Phải bằng nhau |
| State relative tolerance | `1e-11` |
| State absolute tolerance | `1e-13` |

Kết quả thực tế nghiêm hơn yêu cầu: mọi peak lệch 0 sample và state error bằng 0 trong phép đo hiện tại.

## Golden vectors

| Vector | Samples | Python peaks | C peaks | Peak lệch lớn nhất | Kết quả |
|---|---:|---:|---:|---:|---|
| Synthetic upright | 7.200 | 19 | 19 | 0 | Pass |
| Synthetic inverted + drift + noise | 7.200 | 19 | 19 | 0 | Pass |
| QTDB `sel100`, 0–60 s | 21.600 | 78 | 78 | 0 | Pass |
| QTDB `sel123`, 420–480 s | 21.600 | 48 | 48 | 0 | Pass |
| NSTDB `118e00`, 1740–1800 s | 21.600 | 104 | 104 | 0 | Pass |
| NSTDB `118e_6`, 1740–1800 s | 21.600 | 130 | 130 | 0 | Pass |

Tất cả vector được đưa về 360 Hz, đúng sampling rate mục tiêu của firmware. Manifest lưu source, số sample, SHA-256 của input float64 và danh sách peak Python kỳ vọng.

## State được đối chiếu

Sau mỗi sample, verifier so sánh:

1. energy envelope sau moving integration;
2. adaptive signal level;
3. adaptive noise level;
4. event index hoặc `−1` nếu không có event.

Việc chỉ so peak cuối là chưa đủ: hai implementation có thể tình cờ tạo cùng peak nhưng state đã trôi và sẽ khác nhau ở record dài hơn. State trace giúp phát hiện sai khác ngay tại sample đầu tiên.

## Kích thước state

Host C dùng `double` và báo `sizeof(adaptive_pt_t) = 880 byte`.

Con số này không phải RAM cuối của ESP32. Bản firmware có thể giảm bằng:

- `float32` cho SOS, envelope, levels và energy ring;
- `int16_t` cho raw history;
- bỏ coefficient khỏi từng instance và đặt `static const` trong flash.

Ước lượng trước đây khoảng 0,35–0,5 KB vẫn cần được xác nhận bằng `sizeof` và linker map của bản Xtensa.

## Các bước parity còn lại

### Giai đoạn 1 — Host reference C

- [x] Cùng thuật toán với Python.
- [x] Cùng hệ số SOS 360 Hz.
- [x] Golden vectors synthetic, QTDB và NSTDB.
- [x] Peak và state parity.

### Giai đoạn 2 — Firmware-oriented C

- [x] Chuyển state sang `float32`/`int16_t`.
- [x] Đặt hệ số immutable trong flash.
- [x] Chạy lại golden vectors và cho phép lệch tối đa 1 sample.
- [x] Chạy benchmark QTDB locked và NSTDB đầy đủ sau lượng tử hóa.

### Giai đoạn 3 — ESP32 integration

- [ ] Tích hợp vào task sample-by-sample.
- [ ] Replay cùng vector qua firmware/serial hoặc unit-test target.
- [ ] So output ESP32 với host C.
- [ ] Đo stack high-water, minimum free heap, IRAM/DRAM/flash delta.
- [ ] Kiểm tra sustained 360 Hz và watchdog.

## Artefact tái lập

- Python reference: `src/ecg_benchmark/rpeaks.py`
- Host C reference: `experiments/adaptive_pt_portable_host.c`
- int16/float32 C: `experiments/adaptive_pt_portable_int16_host.c`
- Verifier: `experiments/verify_adaptive_pt_python_c.py`
- int16 verifier: `experiments/verify_adaptive_pt_int16.py`
- Kết quả: `outputs/adaptive_pt_python_c_parity/parity_results.csv`
- Manifest/expected peaks: `outputs/adaptive_pt_python_c_parity/golden_vectors.json`
- int16 results: `outputs/adaptive_pt_int16_parity/summary.json`
- int16 QTDB benchmark: `outputs/adaptive_int16_qtdb_locked/`
- int16 NSTDB benchmark: `outputs/adaptive_int16_nstdb/`

Chạy lại:

```powershell
.\.venv\Scripts\python.exe experiments\verify_adaptive_pt_python_c.py
```

Verifier tự compile DLL bằng GCC, chạy cả Python và C rồi trả exit code khác 0 nếu bất kỳ vector nào không đạt.
