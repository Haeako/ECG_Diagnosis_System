# Tổng hợp mô hình phát hiện rung nhĩ (AF)

**Latency 7 nhánh AF, 05/11/2026:** [kết quả và protocol](../../../docs/ECGFOUNDER_AF_COMPARISON.md#latency-từng-nhánh-trên-cohort-afdb-riêng--05112026). Đã đo riêng 720 cửa sổ AFDB/nhánh, batch 1, hai thread. p50/p95 (ms): LR RR **20,57/29,19**; RF RR **25,94/43,46**; ET float RR **24,56/32,35**; ET INT8 RR **17,79/24,80**; XGBoost RR **20,11/30,87**; ConvNeXt FP32 **140,66/181,40**; ECGFounder **51,77/71,91**. Nhánh RR gồm SWT + runtime C tăng dần + classifier riêng; runtime C giữ cả tác vụ ET INT8 khi tính feature cho model float. Nhánh waveform gồm preprocessing + mạng riêng. Không tính acquisition chung, tải/warmup model, thu cửa sổ hoặc truyền thông. Đây là cohort latency chọn bằng header tại 10% timeline, có 90 giây warmup RR; accuracy AFDB vẫn dùng 18.470 hàng chung/21.616 thuần nhãn. Coverage RR của cohort latency **90,42%** có mẫu số riêng. [Raw latency và footprint](../benchmark_runs/af_branch_latency_20261105/latency_summary.json), [audit độc lập](../benchmark_runs/af_branch_latency_20261105/independent_audit.json).

**MLP fusion và latency server, 05/11/2026:** [kết quả chạy thực tế](../../../docs/AF_FUSION_MLP_LATENCY.md). Giữ nguyên split SHDB-AF và 13 feature; MLP 13→16→8→1 đạt BA **89,05%**, F1 AF **80,94%**, thấp hơn LR **92,02% / 86,16%** trên cùng 1.024 hàng test. Đo 1.026 cửa sổ cho mỗi pipeline, batch 1, hai thread: thời gian tính toán chủ động LR p50/p95 **72,23/100,98 ms**; MLP **72,14/102,69 ms**, bao gồm xử lý buffer và JSON. Các số này không sleep chờ snapshot +1 giây; theo policy, độ trễ ra quyết định suy ra khoảng **1.009 ms sau cuối khối**, chưa tính thu nhận/BLE/web. LR tiếp tục là fusion hiện tại.

**So sánh 7 model AF, 05/11/2026:** [bảng đầy đủ trên sáu record AFDB](../../../docs/ECGFOUNDER_AF_COMPARISON.md). Đã chạy 21.740 khối nguồn, xác nhận 21.616 khối thuần nhãn; bảng chính dùng cùng **18.470 hàng** hợp lệ (coverage chung **85,45%**). ECGFounder giữ nguyên đầu **150 nhãn gốc**, AF index 5, đạt **BA 96,28%, F1 AF 94,26%, AUC 0,9913**; Extra Trees float đạt BA 92,38%. LR trong bảng là baseline RR độc lập. Tất cả trọng số giữ nguyên, ngưỡng float 0,5 và ngưỡng INT8 10.240/20.400; không train hoặc tune bằng tập đánh giá. [Metric và confusion matrix](../benchmark_runs/af_models_full_afdb_20261105_v2/metrics.json).

**Nhánh giải thích đã triển khai:** SWT–SepConv → RR server → nguyên runtime và Extra Trees INT8 của device đã freeze → AF + SHAP. Đã giải thích 908 quyết định test hợp lệ (782 cửa sổ AF), dùng background chỉ từ train; xem [kết quả và biểu đồ XAI](../../../docs/AF_SERVER_RR_XAI.md). Nhánh phụ chưa đưa score mới vào LR fusion.

**Pipeline device–server + LR fusion, 03/11/2026:** đã replay 294 đoạn SHDB-AF, 5.292 quyết định thuộc 93 bệnh nhân; chia train/validation/test theo bệnh nhân 55/19/19. Device giữ nguyên C + Extra Trees INT8; server dùng ECGFounder một lead với đầu 150 nhãn gốc cho score AF, SWT–SepConv cho R-peaks/RR/BPM và SQI, không thêm denoiser. Trên cùng 1.024 quyết định paired test, BA device **85,45%**, server **90,00%**, LR fusion **92,02%**; F1 tương ứng **75,37% / 81,44% / 86,16%**. Các ngưỡng chọn trên validation. Chênh lệch BA fusion so với server là **+2,01 điểm phần trăm**, CI bootstrap theo bệnh nhân 95% **[−0,03; +4,93] điểm**, chưa loại trừ 0. Xem [pipeline và kết quả](../../../docs/AF_DEVICE_SERVER_FUSION_PIPELINE.md), [kết quả máy đọc được](../benchmark_runs/af_fusion_published_swt_20261103/training_summary.json). Đây là host replay, không thay thế các benchmark bên dưới hoặc kiểm thử live ESP32/BLE.

Báo cáo này hợp nhất các kết quả phát hiện rung nhĩ (atrial fibrillation, AF) hiện có trong workspace. Các bảng được chia theo bộ dữ liệu và giao thức đánh giá; không so sánh trực tiếp những metric được tính trên cửa sổ, giây có nhãn hoặc các bộ dữ liệu khác nhau.

## Tóm tắt

- **Lựa chọn detector qua so sánh downstream:** Detector firmware hiện tại (liên tục) + Extra Trees INT8. Đã so sánh 5 detector, 19 điều kiện AFDB, sàng lọc trên LTAFDB và xác nhận theo 93 bệnh nhân SHDB-AF; chi tiết ở Mục 8.

- Trong đánh giá AFDB hiện hành, Extra Trees float đạt balanced accuracy (BA) **0,8926**, AUC **0,9729**, độ bao phủ **98,59%** và recall AF tính trên toàn bộ thời gian AF **82,96%**.
- Bản Extra Trees INT8 chạy bằng C đạt BA **0,8899**, AUC **0,9708** và recall AF **82,82%** trên cùng giao thức AFDB. Đo kernel phân loại trên ESP32 cho p50 **391,58 µs**, kích thước model trong Flash **141.881 B** và RAM làm việc inference **13 B**.
- Trên đánh giá chuyển miền với LTAFDB và SHDB-AF, các mô hình cây đạt độ nhạy cao, nhưng hiệu năng và tỷ lệ abstain thay đổi theo tập dữ liệu. Kết quả này là đánh giá trên waveform ngoài, không phải nghiên cứu lâm sàng tiến cứu.
- Hai thử nghiệm học sâu lịch sử trên AFDB dùng cửa sổ 10 giây thuần nhãn: **encoder ECGFounder đóng băng + đầu LR do dự án train** đạt AUC **0,9492**, BA **0,9035** ở ngưỡng 0,50; ConvNeXt đạt AUC **0,8904**, BA **0,7869** cùng ngưỡng. Các số này thuộc giao thức lịch sử riêng; kết quả đầu gốc 150 nhãn nằm trong bảng so sánh 7 model được liên kết ở đầu báo cáo.

## 1. Mô hình RR trên AFDB

Đợt chạy dùng 6 waveform AFDB (60,393 giờ), 19 điều kiện tín hiệu, tổng cộng 114 jobs. Bốn mô hình float và một mô hình Extra Trees INT8 được giữ cố định; không huấn luyện lại hoặc hiệu chỉnh ngưỡng trong lượt đánh giá. Dữ liệu AFDB đã xuất hiện trong các lần phát triển trước, vì vậy đây không phải kiểm định tiến cứu hay tập thử nghiệm chưa từng được xem xét trong dự án.

| Mô hình | Đầu ra | AUC điểm thô | BA | Độ bao phủ | Recall AF trên toàn bộ thời gian AF |
|---|---|---:|---:|---:|---:|
| Logistic Regression | thô | 0,9767 | 0,8883 | 98,59% | 80,44% |
| Logistic Regression | xác nhận 3/4 | 0,9767 | 0,8764 | 98,59% | 77,29% |
| Random Forest | thô | 0,9764 | 0,8478 | 98,59% | 72,27% |
| Random Forest | xác nhận 3/4 | 0,9764 | 0,8160 | 98,59% | 65,10% |
| Extra Trees float | thô | 0,9729 | **0,8926** | 98,59% | **82,96%** |
| Extra Trees float | xác nhận 3/4 | 0,9729 | 0,8809 | 98,59% | 79,60% |
| XGBoost | thô | 0,9737 | 0,8234 | 98,59% | 68,19% |
| XGBoost | xác nhận 3/4 | 0,9737 | 0,7867 | 98,59% | 60,09% |
| Extra Trees INT8 C | thô | 0,9708 | 0,8899 | 98,59% | 82,82% |
| Extra Trees INT8 C | xác nhận 3/4 | 0,9708 | 0,8785 | 98,59% | 79,44% |

BA được tính trên phần thời gian có kết luận; độ bao phủ dùng toàn bộ thời gian có nhãn làm mẫu số. Recall AF ở cột cuối cũng tính cả thời gian bị abstain. AUC ở cả hai chế độ dùng điểm thô, không phải xác suất đã hiệu chỉnh riêng cho chế độ xác nhận.

### Ảnh hưởng của bộ lọc chất lượng tín hiệu

Bộ lọc chỉ quyết định có xuất kết luận ở thời điểm đó hay không; luồng tính trạng thái AF vẫn tiếp tục. Bảng dưới đây dùng Extra Trees float, đầu ra thô, trên tín hiệu gốc và hai tình huống nhiễu nghiêm trọng. Mỗi ô ghi **BA / độ bao phủ / tỷ lệ giữ lại thời gian AF**.

| Điều kiện | Không lọc | Orphanidou firmware | Ho [NeuroKit] ≥80% |
|---|---|---|---|
| Tín hiệu gốc | 0,8926 / 98,59% / 99,85% | 0,9122 / 85,75% / 74,03% | 0,8930 / 86,86% / 86,05% |
| Nhiễu MA −6 dB | 0,8927 / 98,21% / 99,49% | 0,9125 / 85,05% / 72,15% | 0,8957 / 84,64% / 83,94% |
| Nhiễu EM −6 dB | 0,8682 / 91,28% / 87,83% | 0,8893 / 79,04% / 63,06% | 0,8727 / 78,23% / 73,38% |

BA cao hơn đi kèm độ bao phủ và tỷ lệ giữ lại AF thấp hơn khi bật lọc. Ho là bộ ước lượng chất lượng tín hiệu, không phải mô hình phân loại AF. Bộ lọc dùng cửa sổ 10 giây đã hoàn tất nên có độ trễ khi chất lượng tín hiệu thay đổi.

**Kết quả theo tập điều kiện:** [metrics.csv](../benchmark_runs/af_timeline_20261021/metrics.csv), [gate_metrics.csv](../benchmark_runs/af_timeline_20261021/gate_metrics.csv), [báo cáo lượt chạy](../benchmark_runs/af_timeline_20261021/REPORT_CURRENT.md), [giao thức](../benchmark_runs/af_timeline_20261021/protocol.json), [audit](../benchmark_runs/af_timeline_20261021/audit.json).

## 2. Đánh giá chuyển miền trên waveform ngoài

Các mô hình đã được huấn luyện trước lượt này từ khoảng RR suy ra theo annotation rồi được đóng băng. Ở lượt này RR được đo bằng detector sản xuất đã sửa; không fit trên dữ liệu ngoài, không chọn đặc trưng hoặc hiệu chỉnh ngưỡng bằng các bộ dữ liệu đánh giá.

| Tập dữ liệu | Waveform có annotation | Thời lượng | Tần số lấy mẫu / kênh |
|---|---:|---:|---|
| LTAFDB | 84 | 1.960,6 giờ | 128 Hz / ECG |
| SHDB-AF | 98 | 2.330,7 giờ | 200 Hz / ECG1 |

| Tập dữ liệu | Mô hình | BA | Độ nhạy | Độ đặc hiệu | AUC | Bao phủ | Giữ lại AF | Recall AF hiệu dụng |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| LTAFDB | Logistic Regression | 0,9184 | 0,9254 | 0,9115 | 0,9504 | 97,52% | 97,85% | 90,55% |
| LTAFDB | Random Forest | 0,9258 | 0,9509 | 0,9007 | 0,9663 | 97,52% | 97,85% | 93,05% |
| LTAFDB | Extra Trees float | 0,9193 | 0,9713 | 0,8672 | 0,9567 | 97,52% | 97,85% | 95,04% |
| LTAFDB | XGBoost | 0,9198 | 0,9456 | 0,8940 | 0,9651 | 97,52% | 97,85% | 92,52% |
| LTAFDB | Extra Trees INT8 C | 0,9120 | 0,9734 | 0,8505 | 0,9545 | 97,52% | 97,85% | 95,24% |
| SHDB-AF | Logistic Regression | 0,8956 | 0,8693 | 0,9219 | 0,9228 | 99,18% | 99,69% | 86,66% |
| SHDB-AF | Random Forest | 0,8953 | 0,8893 | 0,9013 | 0,9481 | 99,18% | 99,69% | 88,65% |
| SHDB-AF | Extra Trees float | 0,8929 | 0,9025 | 0,8833 | 0,9418 | 99,18% | 99,69% | 89,97% |
| SHDB-AF | XGBoost | 0,8927 | 0,8879 | 0,8974 | 0,9485 | 99,18% | 99,69% | 88,51% |
| SHDB-AF | Extra Trees INT8 C | 0,8871 | 0,9046 | 0,8695 | 0,9401 | 99,18% | 99,69% | 90,18% |

Các chỉ số phân loại được tính trên số giây đã có dự đoán; độ bao phủ và giữ lại AF thể hiện các giây abstain. Recall AF hiệu dụng dùng toàn bộ giây AF có nhãn làm mẫu số. LTAFDB không có ánh xạ mã bệnh nhân được tải cùng dữ liệu nên kết quả gộp theo waveform, không xem như bootstrap theo bệnh nhân. Đợt chạy gồm 182 waveform, đã kiểm tra checksum các tệp ECG và replay C; không gồm ma trận thêm nhiễu hoặc bộ lọc Ho.

**Kết quả chi tiết:** [metrics.csv](../benchmark_runs/af_external_20261022/metrics.csv), [báo cáo lượt chạy](../benchmark_runs/af_external_20261022/REPORT_CURRENT.md), [giao thức và hash đầu vào](../benchmark_runs/af_external_20261022/protocol.json), [audit độc lập](../benchmark_runs/af_external_20261022/audit.json).

## 3. Mô hình học sâu một chuyển đạo trên AFDB

Hai thử nghiệm này phân loại các cửa sổ ECG 10 giây thuần nhãn. AFDB không nằm trong tập huấn luyện của chính các mô hình này, nhưng đã xuất hiện trong các thử nghiệm khác của dự án. Chỉ cửa sổ có annotation thuần được chấm; các đoạn chưa rõ hoặc cắt qua chuyển nhịp bị loại. Do cách chia mẫu và đầu vào khác hệ thống RR ở trên, không so sánh BA trực tiếp.

| Mô hình | Ngưỡng | Số cửa sổ | BA | Độ nhạy | Độ đặc hiệu | F1 | AUC |
|---|---:|---:|---:|---:|---:|---:|---:|
| ECGFounder encoder đóng băng + đầu Logistic Regression | 0,500 | 21.616 | **0,9035** | 0,8621 | 0,9450 | 0,8666 | **0,9492** |
| ECGFounder encoder đóng băng + đầu Logistic Regression | 0,535 | 21.616 | 0,9024 | 0,8593 | 0,9455 | 0,8656 | 0,9492 |
| ConvNeXtV2 1D float32 | 0,50 | 21.616 | 0,7869 | 0,7283 | 0,8455 | 0,6982 | 0,8904 |
| ConvNeXtV2 1D float32 | 0,75 | 21.616 | 0,6613 | 0,3477 | 0,9750 | 0,4947 | 0,8904 |
| ConvNeXtV2 1D TFLite lượng tử hóa | 0,50 | 21.616 | 0,7616 | 0,6774 | 0,8459 | 0,6660 | 0,8747 |
| ConvNeXtV2 1D TFLite lượng tử hóa | 0,75 | 21.616 | 0,6533 | 0,3295 | 0,9771 | 0,4767 | 0,8747 |

**Quy mô và ghi chú triển khai:** ECGFounder dùng encoder FP32 trích xuất từ checkpoint khoảng 369,8 MB; encoder khoảng 123,0 MB, chưa lượng tử hóa. Đây là encoder đóng băng với đầu phân loại logistic được huấn luyện riêng, không phải fine-tune end-to-end. ConvNeXt có 766.818 tham số; trọng số FP32 khoảng 3.067.272 B, bản TFLite lượng tử hóa 1.125.000 B. Đầu vào/đầu ra INT8 nhưng còn 41 phép tính float nên không phải suy luận integer hoàn toàn. Thời gian chạy trên điện thoại chưa được đo cho cả hai.

**Tài liệu:** [ECGFounder report](../benchmark_runs/af_ecgfounder_singlelead_20261028/REPORT.md), [metrics ECGFounder](../benchmark_runs/af_ecgfounder_singlelead_20261028/afdb_metrics.csv), [ConvNeXt report](../benchmark_runs/af_convnext_singlelead_20261028_int8_final/REPORT.md), [metrics ConvNeXt](../benchmark_runs/af_convnext_singlelead_20261028_int8_final/afdb_external_metrics.csv).

## 4. Bản INT8 và số đo trên ESP32

Đo trên ESP32-D0WD-V3, ESP-IDF 6.0.1, CPU 160 MHz. Đây là thời gian của kernel phân loại trên vector đặc trưng INT8 cố định, 10.000 lần lặp trong 101 batch; **không bao gồm trích xuất đặc trưng**, không đo độ chính xác trên thiết bị và không phải độ trễ end-to-end ECG.

| Mô hình | p50 | p95 | p99 | Tối đa | Model trong Flash | RAM làm việc inference |
|---|---:|---:|---:|---:|---:|---:|
| Logistic Regression INT8 | 8,160 µs | 8,160 µs | 8,170 µs | 9,730 µs | 43 B | 17 B |
| Random Forest INT8 | 607,320 µs | 607,320 µs | 607,320 µs | 620,620 µs | 215.833 B | 13 B |
| Extra Trees INT8 | 391,580 µs | 391,630 µs | không xuất | 405,550 µs | 141.881 B | 13 B |

So sánh lượng tử hóa được đo phía máy chủ trên tập AFDB khác: 18 record huấn luyện, 7 record kiểm tra, 20.194 cửa sổ. BA float → INT8 lần lượt là **0,90221 → 0,89822** (Logistic Regression), **0,86641 → 0,86466** (Random Forest), **0,91576 → 0,91291** (Extra Trees). AUC tương ứng: **0,98315 → 0,97749**, **0,98092 → 0,98013**, **0,98166 → 0,97908**. Tỷ lệ dự đoán giống nhau là **98,98%**, **99,47%**, **99,47%**. Đây là một thí nghiệm và tập kiểm tra riêng, không thay thế kết quả AFDB ở Mục 1.

Model Flash và RAM trong bảng là footprint của kernel/model theo báo cáo benchmark, không phải tổng bộ nhớ của toàn thiết bị hay toàn pipeline. Báo cáo phần cứng cũng lưu ý kích thước image ứng dụng ở lượt đo mới khác một profile cũ; dùng số đo mới tại tài liệu nguồn.

**Tài liệu:** [báo cáo benchmark ESP32](../benchmark_runs/device_bench_20261023_sqi_af/REPORT.md), [báo cáo AFDB hiện hành và artifact INT8](../benchmark_runs/af_timeline_20261021/REPORT_CURRENT.md).

## 5. Kết quả lịch sử và phạm vi so sánh

Workspace còn các kết quả AF lịch sử trong [REPORT_BPM_AF.md](../benchmark_runs/non_sqi_20261019/REPORT_BPM_AF.md) và [af_historical_gate_results.csv](../benchmark_runs/non_sqi_20261019/af_historical_gate_results.csv). Đó là các lượt chạy cũ, có cấu hình detector, đặc trưng, cửa sổ và gate khác. Một số gate trong báo cáo cũ là phép xấp xỉ hoặc tên lịch sử, không đồng nhất với các bộ lọc đã triển khai và đánh giá lại ở Mục 1. Vì vậy báo cáo này không trộn bảng lịch sử vào metric hiện hành.

## 6. Cách đọc metric và giới hạn

- **BA** là trung bình độ nhạy và độ đặc hiệu; cần đọc cùng độ bao phủ khi hệ thống có thể abstain.
- **AUC** đo khả năng xếp hạng điểm dự đoán trên các ngưỡng; AUC cao không đồng nghĩa với hiệu quả lâm sàng.
- **Recall AF hiệu dụng** tính cả giây AF bị abstain trong mẫu số; **tỷ lệ giữ lại AF** chỉ mô tả lượng thời gian AF được bộ lọc chấp nhận.
- Kết quả học sâu tính theo cửa sổ 10 giây; kết quả RR chính tính theo giây/timeline. Các đơn vị đánh giá khác nhau.
- AFDB đã được xem trong quá trình phát triển dự án. Bộ dữ liệu ngoài giúp đánh giá chuyển miền trên các waveform cụ thể, nhưng không thay thế xác nhận tiến cứu theo bệnh nhân.
- Đo kernel trên ESP32 không bao gồm detector, trích xuất đặc trưng, thu nhận ECG, bộ lọc chất lượng, công suất hoặc độ chính xác trên thiết bị. Không suy ra độ trễ hoặc mức tiêu thụ năng lượng của toàn hệ thống từ các con số này.
- Các kết quả không tự chứng minh khả năng chẩn đoán, độ an toàn hoặc mức sẵn sàng sử dụng lâm sàng.

## 7. FastQRS → AF so với annotation → AF (31/10/2026)

Đánh giá end-to-end mới trên cùng 6 waveform AFDB/ECG1, tổng cộng 60.393 giờ tín hiệu gốc. Mô hình Extra Trees float và INT8 giữ nguyên; không huấn luyện lại, không chỉnh ngưỡng, không SQI hoặc khử nhiễu bổ sung.

`.qrs` cung cấp mốc nhịp tham chiếu; `.atr` cung cấp nhãn AF. `.qrs` là annotation tự động chưa được hiệu đính toàn bộ, không xem là đỉnh R chuẩn do chuyên gia duyệt. Các record kiểm tra tách khỏi tập huấn luyện mô hình nhưng đã xuất hiện trong quá trình phát triển dự án.

FastQRS C dùng khối 10 giây không chồng lấn và lọc hai chiều trong khối. Dự đoán chỉ được tính là có sau khi thu xong khối, không hồi tố. Hai nhánh annotation lần lượt có sẵn ngay tại đỉnh R và trả theo cùng khối 10 giây. Nhánh annotation theo khối là đối chứng chính để tách lỗi detector khỏi độ trễ gom khối.

### Kết quả toàn timeline có nhãn

Chấm theo giây, giữ cả giây chuyển nhịp. BA/F1/AUC tính trên giây có kết luận; bao phủ và recall AF hiệu dụng tính cả abstain. AUC của chế độ xác nhận vẫn dùng điểm thô.

| Mô hình | Nguồn RR / thời điểm sẵn có | Đầu ra | BA | F1 | AUC | Bao phủ | Recall AF hiệu dụng |
|---|---|---|---:|---:|---:|---:|---:|
|ExtraTrees_float|Annotation, có ngay tại đỉnh R|thô|0.9148|0.8854|0.9766|98.09%|84.11%|
|ExtraTrees_float|Annotation, có ngay tại đỉnh R|xác nhận 3/4|0.9015|0.8751|0.9766|98.09%|80.57%|
|ExtraTrees_float|Annotation, trả theo khối 10 giây|thô|0.9131|0.8830|0.9750|98.07%|83.85%|
|ExtraTrees_float|Annotation, trả theo khối 10 giây|xác nhận 3/4|0.8997|0.8723|0.9750|98.07%|80.27%|
|ExtraTrees_float|FastQRS C, trả theo khối 10 giây|thô|0.8917|0.8551|0.9660|97.45%|83.70%|
|ExtraTrees_float|FastQRS C, trả theo khối 10 giây|xác nhận 3/4|0.8852|0.8527|0.9660|97.45%|81.04%|
|ExtraTrees_INT8_C|Annotation, có ngay tại đỉnh R|thô|0.9122|0.8811|0.9745|98.09%|83.89%|
|ExtraTrees_INT8_C|Annotation, có ngay tại đỉnh R|xác nhận 3/4|0.8988|0.8707|0.9745|98.09%|80.28%|
|ExtraTrees_INT8_C|Annotation, trả theo khối 10 giây|thô|0.9105|0.8787|0.9729|98.07%|83.60%|
|ExtraTrees_INT8_C|Annotation, trả theo khối 10 giây|xác nhận 3/4|0.8969|0.8679|0.9729|98.07%|79.97%|
|ExtraTrees_INT8_C|FastQRS C, trả theo khối 10 giây|thô|0.8877|0.8490|0.9633|97.45%|83.30%|
|ExtraTrees_INT8_C|FastQRS C, trả theo khối 10 giây|xác nhận 3/4|0.8807|0.8456|0.9633|97.45%|80.57%|

### So sánh trên cùng các giây cả hai nhánh đều có dự đoán

Đối chiếu annotation theo khối 10 giây và FastQRS trên phần giao thời gian được cả hai chấp nhận. Bảng này hỗ trợ phân tích lỗi; bảng toàn timeline phía trên mới phản ánh phần thời gian bị bỏ qua.

|Mô hình|Nguồn RR|Số giây chung|BA|F1|AUC|
|---|---|---:|---:|---:|---:|
|ExtraTrees_float|Annotation, trả theo khối 10 giây|208026|0.9131|0.8842|0.9749|
|ExtraTrees_float|FastQRS C, trả theo khối 10 giây|208026|0.8914|0.8541|0.9667|
|ExtraTrees_INT8_C|Annotation, trả theo khối 10 giây|208026|0.9104|0.8799|0.9727|
|ExtraTrees_INT8_C|FastQRS C, trả theo khối 10 giây|208026|0.8876|0.8481|0.9640|

### Episode AF và cảnh báo

|Mô hình|Nguồn RR|Đầu ra|Episode bắt được / tổng|Recall episode|Trễ trung vị (giây)|Cảnh báo sai bắt đầu / giờ non-AF|
|---|---|---|---:|---:|---:|---:|
|ExtraTrees_float|Annotation, có ngay tại đỉnh R|thô|48 / 65|73.85%|14.5000|3.8927|
|ExtraTrees_float|Annotation, có ngay tại đỉnh R|xác nhận 3/4|40 / 65|61.54%|36.0000|1.5191|
|ExtraTrees_float|Annotation, trả theo khối 10 giây|thô|49 / 65|75.38%|18.0000|3.7978|
|ExtraTrees_float|Annotation, trả theo khối 10 giây|xác nhận 3/4|37 / 65|56.92%|37.0000|1.6141|
|ExtraTrees_float|FastQRS C, trả theo khối 10 giây|thô|51 / 65|78.46%|17.0000|5.0558|
|ExtraTrees_float|FastQRS C, trả theo khối 10 giây|xác nhận 3/4|39 / 65|60.00%|38.0000|2.3736|
|ExtraTrees_INT8_C|Annotation, có ngay tại đỉnh R|thô|48 / 65|73.85%|14.5000|3.7978|
|ExtraTrees_INT8_C|Annotation, có ngay tại đỉnh R|xác nhận 3/4|40 / 65|61.54%|36.0000|1.4716|
|ExtraTrees_INT8_C|Annotation, trả theo khối 10 giây|thô|49 / 65|75.38%|18.0000|3.7028|
|ExtraTrees_INT8_C|Annotation, trả theo khối 10 giây|xác nhận 3/4|37 / 65|56.92%|37.0000|1.5666|
|ExtraTrees_INT8_C|FastQRS C, trả theo khối 10 giây|thô|50 / 65|76.92%|17.0000|4.9846|
|ExtraTrees_INT8_C|FastQRS C, trả theo khối 10 giây|xác nhận 3/4|39 / 65|60.00%|38.0000|2.5160|

Episode được xác định riêng từng record trên lưới 1 giây; giữ cả episode có sẵn ở đầu record. Trễ chỉ tính episode bắt được, không gán trễ giả cho episode bị bỏ sót. Thời gian tính toán và truyền mạng chưa được cộng vào độ trễ.

### Mức thay đổi so với annotation cùng độ trễ

Chênh lệch = FastQRS − annotation theo khối 10 giây. Khoảng 95% dùng 5.000 lần bootstrap ghép cặp theo 6 recording (seed 42), chưa phải khoảng tin cậy từ một đoàn hệ bệnh nhân độc lập.

|Mô hình|Đầu ra|Δ BA [khoảng 95%]|Δ recall AF hiệu dụng [khoảng 95%]|
|---|---|---:|---:|
|ExtraTrees_float|thô|-0.0214 [-0.0344; +0.0092]|-0.0015 [-0.0128; +0.0560]|
|ExtraTrees_float|xác nhận 3/4|-0.0145 [-0.0280; +0.0300]|+0.0077 [-0.0064; +0.0983]|
|ExtraTrees_INT8_C|thô|-0.0227 [-0.0360; -0.0018]|-0.0030 [-0.0157; +0.0412]|
|ExtraTrees_INT8_C|xác nhận 3/4|-0.0162 [-0.0302; +0.0241]|+0.0060 [-0.0088; +0.0895]|

### Chất lượng QRS và giới hạn triển khai

|Dung sai so mốc `.qrs`|Độ nhạy|PPV|F1|RR MAE (ms, cặp khớp liên tiếp)|Bao phủ RR tham chiếu|
|---|---:|---:|---:|---:|---:|
|±50 ms|0.9510|0.9351|0.9430|4.337|90.78%|
|±150 ms|0.9897|0.9731|0.9814|6.064|97.55%|

Runtime AF giữ 30 RR, stride 15, reset khi RR ngoài 250–2500 ms và xác nhận 3/4 có warmup bốn quyết định. Không tự thêm TTL; tuổi đầu ra phải đọc cùng coverage, vì runtime có thể giữ kết quả cũ khi không có đỉnh mới. Đây là host replay toàn bộ waveform, không phải độ chính xác hoặc độ trễ của pipeline đã tích hợp trên ESP32. AFE dùng resample_poly làm mô phỏng đầu vào chung; không chứng minh tương đương timing AFE trực tiếp.

Audit tính độc lập bằng Python mọi cửa sổ RR/feature, lịch phát, reset và xác nhận; chạy lại score INT8 mọi cửa sổ; kiểm tra tổng confusion counts từ từng record khớp kết quả gộp.

[Metrics](../benchmark_runs/af_fastqrs_e2e_20261031/metrics.csv) · [Từng record](../benchmark_runs/af_fastqrs_e2e_20261031/per_record.csv) · [Episode](../benchmark_runs/af_fastqrs_e2e_20261031/per_record_episodes.json) · [Chênh lệch ghép cặp](../benchmark_runs/af_fastqrs_e2e_20261031/paired_deltas.csv) · [Giao thức và hash](../benchmark_runs/af_fastqrs_e2e_20261031/protocol.json) · [Audit](../benchmark_runs/af_fastqrs_e2e_20261031/audit.json).

Tái chạy (dùng thư mục đầu ra mới):

```powershell
.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_fastqrs_af_e2e.py --output experiment\report\benchmark_runs\af_fastqrs_e2e_repeat
```

### Kiểm tra độc lập và độ cũ của kết quả

Đã replay mới 18 trace qua runtime C; đối chiếu 1,304,484 ô timeline INT8 và tính lại 53,613 cửa sổ Extra Trees float. Hash source, model, đầu vào và artifact khớp protocol.

Phát hiện các khoảng thiếu mốc `.qrs`: runtime không có TTL nên nhánh annotation có thể giữ kết quả cũ rất lâu. Cần phân biệt độ bao phủ số học với độ tươi của kết quả.

|Nguồn RR|Số giây có kết luận cũ hơn 30 giây|Tuổi đầu ra lớn nhất (giây)|
|---|---:|---:|
|reference_instant|3294|835.908|
|reference_10s|3294|835.908|
|fastqrs_10s|0|27.925|

Phân tích độ nhạy sau chạy: nếu bỏ các đầu ra cũ hơn 30 giây, BA INT8 thô lần lượt là reference_instant: **0.9117**; reference_10s: **0.9100**; fastqrs_10s: **0.8877**. Xu hướng giảm BA khi dùng FastQRS vẫn còn. Mốc 30 giây chỉ dùng để kiểm tra độ nhạy; chưa phải TTL được chọn để triển khai.

**Diễn giải lựa chọn:** FastQRS → Extra Trees INT8 hiện có thể tạo đầu ra AF trong host replay, nhưng BA/F1 giảm so với mốc annotation cùng độ trễ, và cảnh báo sai tăng. Kết quả này chưa chứng minh FastQRS là detector tối ưu cho AF hoặc tốt hơn detector firmware đang dùng. Cần so sánh các detector trên cùng giao thức downstream AF trước khi quyết định thay detector sản xuất.

[Audit độc lập](../benchmark_runs/af_fastqrs_e2e_20261031/independent_audit.json) · [Tuổi đầu ra](../benchmark_runs/af_fastqrs_e2e_20261031/output_age_audit.csv) · [Phân tích độ nhạy](../benchmark_runs/af_fastqrs_e2e_20261031/freshness_sensitivity.csv).

Kiểm tra lại artifact sau khi chạy benchmark:

```powershell
.venv\Scripts\python.exe experiment\src\experiments\af_detection\audit_fastqrs_af_e2e.py --run experiment\report\benchmark_runs\af_fastqrs_e2e_20261031
```

## 8. Lựa chọn detector bằng AF end-to-end và xác nhận ngoài AFDB

**Kết quả lựa chọn theo quy tắc đã khóa: Detector firmware hiện tại (liên tục).** Ứng viên được đưa sang xác nhận là WIM Nguyen C. Ứng viên chưa đạt đủ điều kiện thay detector hiện tại trong phép thử host.

### Thiết kế và điều kiện lựa chọn

- Giữ nguyên Extra Trees INT8, đặc trưng, ngưỡng và xác nhận 3/4. Không fit hoặc hiệu chỉnh bằng tập đánh giá.
- So sánh 5 detector: firmware hiện tại, FastQRS, WIM Nguyen, WIM Elgendi và XQRS C 250 Hz. Tất cả ứng viên nhận cùng đầu ra notch của firmware sau mô phỏng AFE.
- Sàng lọc trên 6 waveform AFDB × 19 điều kiện và 84 record LTAFDB. Chọn đúng một ứng viên theo quy tắc đã ghi trong protocol.
- Khóa ứng viên trước khi chạy 98 record SHDB-AF thuộc 93 bệnh nhân. Phép đối chiếu cuối chỉ gồm ứng viên đã khóa và detector firmware.
- Chỉ số chính dùng đầu ra xác nhận 3/4 trên toàn timeline có nhãn. Báo thêm đầu ra thô, BA/F1/AUC, recall AF tính cả abstain, coverage, episode và cảnh báo sai.
- Các dataset ngoài đã xuất hiện trong thử nghiệm trước của dự án. SHDB-AF được giữ ngoài bước chọn detector của lượt này; không gọi là dữ liệu chưa từng được dự án xem xét.

Quy tắc xác nhận được khóa trước: cận dưới khoảng 95% của ΔBA > 0; cận dưới Δrecall AF hiệu dụng ≥ −0,02; cận trên Δcảnh báo sai/giờ non-AF ≤ +0,5; coverage không giảm quá 0,02. Đồng thời kiểm tra độ bền nhiễu AFDB. Đây là biên kỹ thuật do dự án đặt trước, chưa phải tiêu chuẩn lâm sàng.

### AFDB gốc — đầu ra xác nhận 3/4

![So sánh hiệu năng AF downstream](../../../visualize/results/experiment/report/benchmark_runs/af_detector_selection_20261031/detector_selection_evidence.png)

|Detector|BA|F1|AUC|Bao phủ|Recall AF hiệu dụng|Recall episode|Cảnh báo sai/giờ non-AF|
|---|---:|---:|---:|---:|---:|---:|---:|
|Detector firmware hiện tại (liên tục)|0.8785|0.8449|0.9708|98.59%|79.44%|61.54%|1.9226|
|Detector firmware, trì hoãn theo khối 10 giây|0.8781|0.8443|0.9684|98.59%|79.42%|60.00%|1.9226|
|FastQRS C|0.8778|0.8424|0.9636|97.31%|79.92%|58.46%|2.4923|
|WIM Nguyen C|0.8944|0.8632|0.9667|98.99%|82.70%|61.54%|1.9938|
|WIM Elgendi C|0.8411|0.7960|0.9597|97.07%|70.96%|60.00%|2.0176|
|XQRS C (250 Hz)|0.8531|0.8123|0.9619|96.00%|72.66%|61.54%|2.1837|
|Mốc .qrs theo khối 10 giây|0.8969|0.8679|0.9729|98.07%|79.97%|56.92%|1.5666|

### LTAFDB — bước sàng lọc

|Detector|BA|F1|AUC|Bao phủ|Recall AF hiệu dụng|Recall episode|Cảnh báo sai/giờ non-AF|
|---|---:|---:|---:|---:|---:|---:|---:|
|Detector firmware hiện tại (liên tục)|0.9233|0.9329|0.9545|97.52%|94.12%|70.65%|2.7953|
|Detector firmware, trì hoãn theo khối 10 giây|0.9228|0.9324|0.9537|97.51%|94.08%|70.20%|2.8207|
|FastQRS C|0.9025|0.9146|0.9243|98.22%|93.27%|70.22%|4.3395|
|WIM Nguyen C|0.9212|0.9297|0.9575|95.43%|91.08%|66.05%|2.9269|
|WIM Elgendi C|0.9037|0.9121|0.9493|91.33%|85.10%|59.66%|2.6459|
|XQRS C (250 Hz)|0.9103|0.9201|0.9530|91.64%|87.12%|64.25%|2.7543|

Kiểm tra điều kiện sàng lọc đã khóa (chênh lệch so với firmware):

|Ứng viên|Δ recall AF hiệu dụng (điểm %)|Δ cảnh báo sai/giờ|Δ coverage (điểm %)|Đạt sàng lọc|
|---|---:|---:|---:|---|
|WIM Nguyen C|-3.05|+0.1316|-2.09|Chưa đạt|
|XQRS C (250 Hz)|-7.00|-0.0409|-5.88|Chưa đạt|
|WIM Elgendi C|-9.02|-0.1493|-6.18|Chưa đạt|
|FastQRS C|-0.85|+1.5442|+0.70|Chưa đạt|

### SHDB-AF — xác nhận ứng viên đã khóa

|Detector|BA|F1|AUC|Bao phủ|Recall AF hiệu dụng|Recall episode|Cảnh báo sai/giờ non-AF|
|---|---:|---:|---:|---:|---:|---:|---:|
|Detector firmware hiện tại (liên tục)|0.8898|0.7596|0.9401|99.18%|88.82%|73.80%|2.2224|
|WIM Nguyen C|0.8889|0.7617|0.9395|99.46%|87.90%|72.04%|2.1445|

Chênh lệch dưới đây bằng ứng viên − detector firmware. Bootstrap ghép cặp 5.000 lần theo 93 bệnh nhân (seed 42); các record cùng bệnh nhân được lấy mẫu cùng nhau.

|Chỉ số|Chênh lệch|Khoảng 95%|
|---|---:|---:|
|BA|-0.0009|[-0.0024; +0.0005]|
|Recall AF hiệu dụng|-0.0092|[-0.0197; -0.0031]|
|Cảnh báo sai/giờ non-AF|-0.0779|[-0.3560; +0.0936]|
|Độ bao phủ|+0.0028|[-0.0025; +0.0114]|

|Điều kiện thay detector|Kết quả|
|---|---|
|BA cao hơn với cận dưới khoảng 95% > 0|Chưa đạt|
|Recall AF hiệu dụng không giảm quá biên 2 điểm %|Đạt|
|Cảnh báo sai không tăng quá biên 0,5/giờ|Đạt|
|Độ bao phủ không giảm quá 2 điểm %|Đạt|
|Giữ recall AF qua ma trận nhiễu AFDB|Chưa đạt|

### Độ bền với nhiễu AFDB

Cùng BW/MA/EM ở 24, 18, 12, 6, 0 và −6 dB; nhiễu 10 giây mỗi 30 giây. Bảng dưới dùng đầu ra xác nhận, chấm trên toàn record. Metric riêng trong burst và khoảng không thêm nhiễu được lưu trong CSV.

|Điều kiện|BA firmware|BA ứng viên|Recall AF hiệu dụng firmware|Recall AF hiệu dụng ứng viên|
|---|---:|---:|---:|---:|
|bw_24|0.8803|0.8948|79.81%|82.82%|
|bw_18|0.8786|0.8939|79.47%|82.59%|
|bw_12|0.8786|0.8920|79.49%|82.35%|
|bw_6|0.8784|0.8911|79.41%|82.18%|
|bw_0|0.8778|0.8913|79.33%|82.16%|
|bw_-6|0.8831|0.8878|80.58%|81.32%|
|ma_24|0.8788|0.8933|79.54%|82.55%|
|ma_18|0.8796|0.8916|79.65%|82.12%|
|ma_12|0.8779|0.8886|79.24%|81.60%|
|ma_6|0.8841|0.8888|80.51%|81.47%|
|ma_0|0.8861|0.8810|81.18%|79.15%|
|ma_-6|0.8983|0.8442|85.11%|67.25%|
|em_24|0.8771|0.8926|79.12%|82.43%|
|em_18|0.8786|0.8919|79.45%|82.31%|
|em_12|0.8771|0.8940|79.19%|82.62%|
|em_6|0.8796|0.8928|79.87%|82.46%|
|em_0|0.8880|0.8883|82.31%|82.10%|
|em_-6|0.8951|0.8844|79.00%|82.57%|

![Độ bền recall AF trước nhiễu](../../../visualize/results/experiment/report/benchmark_runs/af_detector_selection_20261031/noise_recall_delta.png)


### Đầu ra thô — phân tích bổ sung

|Dataset|Detector|BA|Recall AF hiệu dụng|Cảnh báo sai/giờ non-AF|
|---|---|---:|---:|---:|
|AFDB|Detector firmware hiện tại (liên tục)|0.8899|82.82%|4.2250|
|AFDB|FastQRS C|0.8862|83.00%|4.9846|
|AFDB|WIM Nguyen C|0.8992|84.98%|4.7472|
|AFDB|WIM Elgendi C|0.8626|76.81%|5.3881|
|AFDB|XQRS C (250 Hz)|0.8766|78.82%|5.2457|
|LTAFDB|Detector firmware hiện tại (liên tục)|0.9120|95.24%|7.7088|
|LTAFDB|FastQRS C|0.8927|95.65%|10.0628|
|LTAFDB|WIM Nguyen C|0.9135|92.95%|8.3770|
|LTAFDB|WIM Elgendi C|0.9091|89.35%|7.9599|
|LTAFDB|XQRS C (250 Hz)|0.9096|90.48%|8.5163|
|SHDB-AF|Detector firmware hiện tại (liên tục)|0.8871|90.18%|5.8361|
|SHDB-AF|WIM Nguyen C|0.8874|89.54%|5.6691|

### Độ tươi kết quả và kiểm chứng

Phân tích độ nhạy 30 giây được tính lại bằng cách chuyển đầu ra cũ hơn 30 giây thành abstain, giữ **toàn bộ giây có nhãn** trong mẫu số coverage và recall AF hiệu dụng. Đây là phân tích bổ sung; chưa đổi TTL của firmware. `fresh_30s` trong CSV gốc là tầng thời gian có đầu ra tươi; bảng dưới lấy từ `freshness_gated_metrics.csv` với mẫu số toàn timeline.

|SHDB-AF — đầu ra xác nhận, bỏ đầu ra cũ >30 giây|BA|Bao phủ|Recall AF hiệu dụng|
|---|---:|---:|---:|
|Detector firmware hiện tại (liên tục)|0.8899|99.05%|88.66%|
|WIM Nguyen C|0.8889|99.44%|87.89%|

Audit độc lập đạt: 296 jobs, 1,498 replay C mới, 176,090,872 ô timeline và 72,128 kiểm tra counts. Đã xác nhận khóa lựa chọn có trước bước SHDB-AF và tái lập bootstrap theo bệnh nhân.

[Audit độc lập](../benchmark_runs/af_detector_selection_20261031/independent_audit.json) · [Kiểm tra đầu vào](../benchmark_runs/af_detector_selection_20261031/input_audit.json) · [Độ nhạy theo tuổi đầu ra](../benchmark_runs/af_detector_selection_20261031/freshness_gated_metrics.csv) · [Tuổi đầu ra](../benchmark_runs/af_detector_selection_20261031/output_age_summary.csv).


### Phạm vi của kết luận

Lựa chọn này dựa trên các C port đã có và mô hình AF đang khóa. Detector firmware giữ trạng thái liên tục; các ứng viên batch thu đủ 10 giây mới phát kết quả. So sánh chính giữ đúng thời điểm sẵn có đó. Nhánh Production_10s chỉ dùng để phân tích riêng ảnh hưởng của độ trễ gom khối.

Runtime chưa có TTL. BA/AUC được tính trên giây có dự đoán; recall AF hiệu dụng và coverage ở phân tích toàn timeline giữ phần abstain trong mẫu số.

Đây là bằng chứng lựa chọn trong host replay, chưa tự xác nhận hiệu năng của pipeline tích hợp trên board hoặc hiệu quả lâm sàng. Các số latency/RAM ESP32 đã đo trước đây là bằng chứng khả thi của kernel riêng, không cộng chúng thành một số đo end-to-end mới.

Kết luận áp dụng cho shortlist 4 ứng viên C đã khai báo. Các detector khác như WIM Zhai và các mô hình QRS học sâu chưa nằm trong lượt so sánh này; không diễn giải lựa chọn thành tối ưu toàn bộ thuật toán đã tồn tại. SQI và fusion chưa được thêm vào đường AF của phép thử này.

[Giao thức đã khóa](../benchmark_runs/af_detector_selection_20261031/protocol.json) · [Khóa lựa chọn trước SHDB-AF](../benchmark_runs/af_detector_selection_20261031/selection_lock.json) · [Quyết định và điều kiện](../benchmark_runs/af_detector_selection_20261031/decision.json) · [Metrics AFDB/LTAFDB](../benchmark_runs/af_detector_selection_20261031/metrics_afdb_ltafdb.csv) · [Metrics SHDB-AF](../benchmark_runs/af_detector_selection_20261031/metrics_shdbaf.csv) · [Chênh lệch ghép cặp](../benchmark_runs/af_detector_selection_20261031/paired_confirmation.csv).

Mã chạy và kiểm chứng: [benchmark](../../src/experiments/af_detection/run_detector_selection.py), [adapter chạy phân mảnh trên Windows](../../src/experiments/af_detection/run_detector_selection_shard.py), [audit độc lập](../../src/experiments/af_detection/audit_detector_selection.py).
