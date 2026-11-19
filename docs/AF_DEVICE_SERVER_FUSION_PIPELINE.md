# Pipeline AF device–server + LR fusion

Cập nhật 03/11/2026. Đã chạy đủ 294 đoạn, 5.292 quyết định thuộc 93 bệnh nhân và train LR fusion. Không train lại các model base. Cấu hình bỏ denoiser riêng; dùng SQI và giữ nguyên hai model server được phát hành.

**Đối chiếu MLP và latency, 05/11/2026:** MLP 13→16→8→1 đã hội tụ, đạt BA 89,05% và F1 AF 80,94% trên cùng 1.024 hàng test; LR đạt 92,02% và 86,16%, nên tiếp tục giữ LR. Đo trực tiếp 1.026 cửa sổ/model, batch 1, hai thread: thời gian tính toán chủ động LR p50/p95 **72,23/100,98 ms**, MLP **72,14/102,69 ms**, bao gồm xử lý buffer và JSON nhưng không sleep chờ snapshot +1 giây. Đường MLP có thêm RR INT8 và SHAP p50/p95 **76,79/104,64 ms**. Theo policy snapshot, độ trễ ra quyết định suy ra khoảng **1.009 ms sau cuối khối**, chưa tính truyền thông. Xem [giao thức, phân rã timing và dữ liệu thô](AF_FUSION_MLP_LATENCY.md).

```text
ECG một lead, full-rate
  ├─ Device: notch → Elgendi/Two-MA + Pan-head → R-peaks/RR/BPM
  │                                      └→ 30 RR, stride 15 → Extra Trees INT8 AF
  │          SQI Orphanidou: cửa sổ 10 s, bước 1 s
  │
  └─ Server: cửa sổ hoàn tất 10 s
       ├─ SWT–SepConv gốc → R-peaks → RR → BPM + template SQI
       └─ ECGFounder 1-lead gốc, giữ đủ đầu 150 nhãn → score AF (index 5)
                         ↓
             Ghép đúng session và availability
                         ↓
          LR fusion: hai score AF + SQI + tuổi kết quả + đặc trưng RR/BPM
                         ↓
                     AF cuối cùng
```

## Model và input

Đã bổ sung nhánh phụ **SWT RR → Extra Trees INT8 của device đã freeze → AF + SHAP** trên server. Nhánh này dùng cùng classifier với device và RR từ SWT; score mới chưa đi vào LR hiện tại. Xem [cấu hình, kết quả và biểu đồ XAI](AF_SERVER_RR_XAI.md).

- **Device:** nguyên C production, gồm QRS detector, 5 đặc trưng RR, model Extra Trees INT8 và raw/confirmed 3/4. Không thay detector hoặc trọng số.
- **Server AF:** nguyên `1_lead_ECGFounder.pth`, load `strict=True`, giữ đầu dense 150×1024. Không dùng đầu AF LR/scaler riêng của thí nghiệm cũ. Xuất sigmoid cho đủ 150 nhãn; fusion V1 chỉ dùng `ATRIAL FIBRILLATION`, index 5. Các nhãn khác được lưu để kiểm tra, chưa có tuyên bố độ chính xác đa bệnh trên device.
- **Server BPM:** nguyên checkpoint SWT–SepConv, preprocessing SWT/derivative và localizer của tác giả. R-peaks → RR hợp lệ 250–2500 ms → `BPM = 60000 / median(RR_ms)` khi có ít nhất hai RR hợp lệ. Không thêm MECG-E hay denoiser khác; DWT/low-pass vốn nằm trong preprocessing gốc của detector vẫn được giữ.
- **SQI:** giữ Orphanidou status/score của device; thêm median leave-one-out beat-template correlation trên input server và các chỉ số RR. Dùng làm feature, chưa hard-gate các đoạn chất lượng kém để làm đẹp coverage.

Cùng waveform sau mô phỏng thu nhận được cấp cho hai nhánh: nguồn ECG mV → resample 360 Hz → AFE high-pass 0,5 Hz → ADC 12 bit → notch C firmware. `corrected / 200` là waveform chung. ECGFounder resample 500 Hz, filter gốc + z-score riêng cửa sổ 10 giây. Mô hình/đầu classifier gốc giữ nguyên; giới hạn filter trong khối đã thu xong là adaptation khi tích hợp.

## Đồng bộ và fallback

Mọi timestamp cùng session. Cửa sổ tín hiệu và thời điểm output có sẵn được lưu riêng. Device dùng processing timestamp, không dùng R timestamp đã căn lùi để giả định kết quả có sớm.

- Server availability = cuối khối + thời gian CPU preprocess/inference thực đo của hai model, có hàng đợi tuần tự; network giả lập 0.
- Snapshot tại cuối khối + 1.000 ms, dùng state device/SQI/server đã có trước hoặc đúng thời điểm đó.
- `server_current` phân biệt response khối đang chờ với cache cũ. Không biến missing probability thành 0.
- Khi có đủ hai nhánh: LR. Thiếu device: dùng server hiện tại hợp lệ. Thiếu server: dùng device hợp lệ. Cả hai thiếu: unavailable.
- Ngưỡng từng route được chọn trên validation, không chọn bằng test.

Đây là host replay và CPU inference, không phải số đo ESP32/network live. Resample nguồn polyphase offline vẫn có local lookahead nhỏ; không gọi là mô phỏng thu nhận causal tuyệt đối.

## Dữ liệu và train LR

Manifest đã khóa trước inference: SHDB-AF ECG1, 98 recording thuộc 93 bệnh nhân. Seed 42, chia theo Subject_ID:

| Split | Bệnh nhân | Recording | Quyết định | Đủ hai nhánh |
|---|---:|---:|---:|---:|
| Train | 55 | 60 | 3.240 | 3.229 |
| Validation | 19 | 19 | 1.026 | 1.022 |
| Test | 19 | 19 | 1.026 | 1.024 |

Mỗi recording lấy ba đoạn theo vị trí 10%, 50%, 90%, độc lập với nhãn; không trùng đoạn. Mỗi đoạn có 90 s warmup device, 180 s đánh giá và 2 s tail. Giữ cả đoạn chuyển nhịp. Target là AF/AFIB tại thời điểm snapshot; các nhịp đã rà soát khác là non-AF. Manifest có group, thời điểm, nhãn và hash input để audit; nhãn không đi vào hai model hoặc hàm ghép.

Fit LR trên các hàng paired của **train**; imputer/scaler cũng chỉ fit train. Cố định C=1, balanced class weights, liblinear, seed 42. Train thêm LR chỉ hai score để làm ablation. Validation chọn ngưỡng theo BA cho LR, từng nhánh và mean baseline. Test chỉ báo kết quả cuối; không refit nếu chưa thắng.

So sánh cùng hàng paired test, đồng thời báo coverage/fallback. Bootstrap ghép cặp 2.000 lần theo bệnh nhân. Đây là test tách khỏi fusion fitting; không tự nhận hoàn toàn chưa xuất hiện trong pretraining của các model foundation.

## Kết quả đã chạy

Test gồm 19 bệnh nhân. Bảng dưới tính trên cùng 1.024 quyết định đủ hai nhánh, bao phủ 99,81% trong 1.026 quyết định test. Mỗi phương pháp được chọn ngưỡng riêng bằng validation; ngưỡng LR là 0,657671.

| Phương pháp | Balanced accuracy | F1 AF | AUC |
|---|---:|---:|---:|
| Device Extra Trees INT8 | 85,45% | 75,37% | 0,9211 |
| Server ECGFounder, đầu gốc | 90,00% | 81,44% | 0,9589 |
| Trung bình hai score | 88,74% | 79,80% | 0,9558 |
| LR chỉ hai score | 90,08% | 81,79% | 0,9643 |
| **LR đầy đủ score + SQI/RR/BPM** | **92,02%** | **86,16%** | **0,9616** |

LR đầy đủ tăng BA **6,56 điểm phần trăm so với device**, **2,01 điểm so với server** và **3,27 điểm so với mean** trong lượt test này. So với LR chỉ hai score, thêm SQI/RR/BPM tăng 1,93 điểm BA. Không chọn lại feature, trọng số hoặc ngưỡng bằng test.

Bootstrap 2.000 lần theo bệnh nhân cho chênh lệch BA so với server: CI 95% **[−0,03; +4,93] điểm phần trăm**. Mức tăng quan sát được đáp ứng mục tiêu tốt hơn hai nhánh chạy riêng trên lượt này, nhưng CI còn chứa 0 nên chưa kết luận ưu thế thống kê chắc chắn.

Hai hàng thiếu device dùng fallback server. Tính toàn bộ 1.026 quyết định test: coverage **100%**, BA **91,96%**, F1 **86,01%**. Thời gian toàn lượt replay và train khoảng **593 s trên host**, không phải thời gian ESP32.

Audit độc lập đạt **394.914 kiểm tra** trên toàn bộ 5.292 hàng: ghép theo availability, split bệnh nhân, 230 hash nguồn/dữ liệu/model, đầu gốc 150 nhãn, công thức RR/BPM, dự đoán LR, ngưỡng validation và bootstrap. Audit kiểm tra output đã lưu, không chạy lại C/model neural hoặc tính lại template correlation từ ECG gốc. Các kiểm thử tập trung cho protocol, adapter và LR cũng đã đạt (41 test).

## Chạy và artifacts

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_published_fusion.py
# Tiếp tục đúng run đã khóa nếu bị gián đoạn:
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_published_fusion.py --resume
```

- [Trạng thái run](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/status.json)
- [Protocol](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/protocol.json)
- [Model base và hash](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/base_models.json)
- [Output ghép](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/paired_predictions.csv)
- [Kết quả LR](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/training_summary.json)
- [LR đã train](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/models/fusion_lr.joblib)
- [Dự đoán fusion](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/models/row_predictions.csv)
- [Audit replay](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/replay_audit.json)
- [Audit độc lập](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/independent_audit.json)

## Transport live

BLE client đã khớp packet v1 20 byte. Wire hiện thiếu AF validity/window endpoints/processing availability; waveform BLE cũng giảm tốc độ với timing danh nghĩa. Run này dùng waveform full-rate từ replay. Đưa cùng contract sang live cần bổ sung transport metadata và kiểm tra trên waveform thực nhận.

Lượt 02/11/2026 ở `af_fusion_sync_20261102` dùng encoder + đầu AF riêng cũ, chỉ là mốc tích hợp lịch sử; không trộn số đo đó với pipeline published-head hiện tại.
