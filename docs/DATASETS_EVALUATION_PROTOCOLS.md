# Các database và protocol đánh giá đã sử dụng

Tài liệu mô tả phạm vi dữ liệu và giao thức thực tế của dự án, không phải protocol mặc định do mỗi database quy định. Không chạy thêm benchmark trong lượt tổng hợp này.

Kết quả tương ứng: [Tổng hợp kết quả theo dataset](DATASETS_RESULTS_SUMMARY.md).

Nguồn ECG đánh giá BPM hiện hành là EDB source holdout; NSTDB chỉ cung cấp noise. MITDB/QTDB chỉ còn vai trò provenance và đối soát lịch sử.

## 1. Bảng tổng quan

| Database | Vai trò trong nghiên cứu | Phạm vi thực tế đã dùng | Protocol chính |
| --- | --- | --- | --- |
| [European ST-T Database — EDB](https://physionet.org/content/edb/1.0.0/) | Đánh giá QRS/RR/BPM hiện hành | 57 record / 114 giờ mỗi điều kiện; channel 0 cố định theo header | Loại 33 source record có đoạn trích trong QTDB. Toàn record; Original + 18 SNR cố định + 3 Mixed. Chỉ công bố ô đủ 57 record. |
| [MIT-BIH Noise Stress Test Database — NSTDB](https://physionet.org/content/nstdb/1.0.0/) | Nguồn nhiễu BW/MA/EM | Kênh noise 0; không dùng cohort ECG dựng sẵn | Cộng vào EDB ở −6/0/6/12/18/24 dB; burst 10 giây từ giây 10 mỗi 30 giây. Mixed chứa cả sáu mức trong từng burst. Chấp nhận noise đã dùng train theo yêu cầu, không suy ra unseen-noise. |
| MITDB và QTDB | Provenance huấn luyện và kiểm tra overlap | Kết quả accuracy đã rút khỏi báo cáo hiện hành | Không dùng làm independent holdout cho checkpoint đã có training exposure. Header QTDB được dùng loại source EDB trùng; raw lịch sử chỉ giữ để đối soát. |
| [MIT-BIH Atrial Fibrillation Database — AFDB](https://physionet.org/content/afdb/1.0.0/) | So sánh 7 phương pháp AF đã đóng băng | 6 record ECG1, 250 Hz; 60,3931 giờ; 21.740 khối 10 giây hoàn chỉnh | Loại 124 khối chuyển nhịp, còn 21.616 khối thuần nhãn; bảng chính dùng 18.470 khối có output của cả 7 model. Ngưỡng float 0,5; INT8 score ≥10.240/20.400. Không train lại hoặc tune theo tập đánh giá. |
| [SHDB-AF](https://physionet.org/content/shdb-af/1.0.1/) | Train/validation/test fusion device–server; đo latency server | 98 recording có annotation / 93 bệnh nhân; ECG1, 200 Hz | Split theo bệnh nhân 55/19/19, seed 42. Mỗi recording lấy 3 đoạn ở 10%/50%/90%; mỗi đoạn 90 s warmup + 180 s đánh giá + 2 s tail. Chọn ngưỡng trên validation; chấm cùng 1.024 hàng paired test và báo fallback trên đủ 1.026 hàng. |
| [Brno University of Technology ECG Quality Database — BUTQDB](https://physionet.org/content/butqdb/1.0.0/) | Đánh giá SQI single-lead | 18 record / 15 người; ECG 1.000 Hz; nguồn 478,73 giờ; 35.773 cửa sổ có nhãn đầy đủ | Cửa sổ 10 s, stride 10 s; nhãn consensus, class 1+2 good, class 3 bad; lấy lớp xấu nhất trong cửa sổ. Loại cửa sổ chứa mẫu chưa gán nhãn. 5-fold OOF tách subject; mọi fit chỉ dùng train fold. |
| [Long Term AF Database — LTAFDB](https://physionet.org/content/ltafdb/1.0.0/) | Đánh giá chuyển miền cho model RR đóng băng; dữ liệu phát triển ConvNeXt trong lượt riêng | 84 waveform; kênh ECG 0, 128 Hz; 1.960,6 giờ | Đánh giá RR ngoài: toàn record, chấm timeline 1 giây, không fit/tune trên dữ liệu ngoài. Lượt train ConvNeXt riêng dùng 72/12 record groups train/validation; vì vậy không gọi LTAFDB là test chưa từng dùng cho ConvNeXt. |

## 2. EDB và NSTDB: protocol QRS/RR/BPM hiện hành

- Cùng 57 record EDB cho mọi implementation; loại cả record nếu có đoạn trích trong 105 header QTDB. Đây là source-record holdout, chưa có bằng chứng patient-disjoint.
- Channel 0 được chọn trước khi đọc nhãn, không mặc định MLII. Source 250 Hz/mV; AFE chung: polyphase 360 Hz, HP causal 0,5 Hz, 200 counts/mV, offset 2048, round/clip ADC 12-bit.
- Chạy full record và reset mỗi record/điều kiện; preprocessing offline riêng của upstream được giữ. Bộ chín phương pháp đã hoàn tất; 16 implementation WIM bổ sung chạy riêng cùng giao thức, không dùng crop MITDB/QTDB lịch sử thay accuracy.
- Noise được bỏ DC và scale RMS cục bộ theo burst; offset xác định từ tên record. Original + BW/MA/EM ở sáu SNR + ba Mixed = 22 điều kiện. Mỗi burst Mixed 10 giây chia thành sáu đoạn bằng nhau ở −6/0/6/12/18/24 dB, xoay thứ tự qua các burst.
- R-peak ghép một-một tại ±50 và ±150 ms; TP/FP/FN gộp micro. RR chỉ chấm cặp matched liên tiếp và báo coverage. BPM là mean-RR cửa sổ 5 giây, giữ cửa sổ thiếu output trong mẫu số coverage.
- Giữ mọi peak trùng; adapter chỉ sắp xếp ổn định theo thời gian, không xóa hoặc dịch peak theo nhãn. Mỗi job lưu raw prediction/hash, qua hai bộ chấm độc lập. Chỉ điền aggregate khi đủ 57 record.
- Không train/tune trên EDB. SWT khai báo MITDB/INCART/QTDB; loại source overlap QTDB. ECG2RR đã dùng NSTDB BW/MA khi train, được chấp nhận theo chính sách giữ ECG gốc ngoài train. Các mạng WIM có source khai báo CPSC2020, chưa tái dựng manifest train từng checkpoint.
- Sàng lọc ESP là thí nghiệm riêng trên 21 đoạn 10 giây cố định, sau warmup: bỏ method có Python max trên 10 ms; workspace C phải có căn cứ và nằm trong budget board. Timing Python không phải timing ESP32. Bản C reset khối 10 giây phải kiểm tra riêng, không mượn nguyên accuracy full-record Python.

Nguồn: [báo cáo QRS/RR/BPM duy nhất](../experiment/report/qrs_rr_bpm/REPORT.md), [protocol bộ chín](../outputs/BPM/edb_holdout_matrix_20261111/protocol.json), [WIM](../outputs/BPM/edb_wim_matrix_20261113/supervisor.json), [ESP screen](../outputs/BPM/esp_python_screen_20261113/audit.json).

## 3. AFDB: protocol so sánh 7 model hiện hành

- Dùng `04936`, `05091`, `05261`, `06453`, `07859`, `08434`, kênh ECG1. Tổng đuôi thiếu khối 10 giây là 15,2 giây.
- Target AF/AFIB = 1; các nhịp khác đã kiểm duyệt, gồm AFL, = 0. Nhãn tại đầu khối; loại khối có thay đổi nhịp nằm nghiêm ngặt trong khối. Còn 6.518 khối AF và 15.098 khối không AF.
- Chạy liên tục toàn record, chỉ reset đầu record. Không bỏ cố định 90 giây; thiếu 30 RR là thiếu output và được tính vào coverage.
- 5 baseline RR cùng nhận peak SWT và 5 feature C, cửa sổ 30 RR, stride 15. Hai model waveform preprocess độc lập từng khối đã hoàn tất; ECGFounder giữ đầu gốc 150 nhãn, AF index 5.
- Cùng 18.470 hàng chung để so model; báo thêm output coverage riêng và metric riêng từng model, gộp và theo record. BA/AUC để trống khi cohort chỉ có một lớp.
- Metrics: BA, sensitivity, specificity, F1 AF, AUC và coverage. Trọng số và ngưỡng giữ nguyên; lưu từng dự đoán, feature/peak, 150 output, lý do loại và hash.
- Bốn baseline cổ điển được train trước bằng 18 record AFDB; sáu record đánh giá nằm ngoài train đó. Không nhầm 20.194 RR-window test lịch sử với 21.616 khối 10 giây hiện hành. AFDB từng xuất hiện trong phát triển dự án; giao nhau với pretraining ECGFounder chưa được xác định.

Nguồn: [báo cáo 7 model](ECGFOUNDER_AF_COMPARISON.md), [protocol đã chạy](../experiment/report/benchmark_runs/af_models_full_afdb_20261105_v2/protocol.json), [manifest train cổ điển](../experiment/report/outputs/af_detection/final_benchmark/experiment.json).

## 4. SHDB-AF: protocol fusion hiện hành

- Split 55/19/19 bệnh nhân tương ứng 60/19/19 recording, tạo 180/57/57 đoạn và 3.240/1.026/1.026 quyết định train/validation/test. Paired hợp lệ tương ứng 3.229/1.022/1.024.
- Ba vị trí lấy đoạn được chọn độc lập với nhãn. Quyết định mỗi 10 giây, snapshot cố định cuối khối +1 giây. Ghép theo session và thời điểm output có sẵn; không dùng kết quả tương lai.
- Target là AF/AFIB tại snapshot; nhịp đã kiểm duyệt khác là âm. **Giữ các khối chuyển nhịp**, khác protocol AFDB thuần nhãn.
- Base models đóng băng. Imputer, scaler và LR/MLP fusion chỉ fit train; ngưỡng từng route chọn trên validation, không tune test.
- So sánh device, ECGFounder, LR và MLP trên cùng paired test; báo thêm coverage và fallback trên đủ 1.026 hàng. Metrics BA, sensitivity, specificity, F1 AF, AUC; bootstrap ghép cặp 2.000 lần theo bệnh nhân.
- Latency server đo riêng trên đủ 1.026 cửa sổ/model, batch 1, 2 thread; warmup không vào thống kê. Báo active-compute p50/p95/p99/max; tách thu cửa sổ 10 giây, policy snapshot +1 giây và truyền thông khỏi thời gian tính toán.
- Có một lượt full-waveform RR ngoài trước đây trên toàn 98 recording / 2.330,7 giờ, chấm timeline 1 giây. Lượt đó khác ba đoạn/record của fusion; không trộn metric hai protocol.

Nguồn: [pipeline fusion](AF_DEVICE_SERVER_FUSION_PIPELINE.md), [manifest](../experiment/report/benchmark_runs/fusion_data_plan_20261103/inventory.json), [MLP và latency](AF_FUSION_MLP_LATENCY.md), [RR full-waveform ngoài](../experiment/report/benchmark_runs/af_external_20261022/REPORT_CURRENT.md).

## 5. BUTQDB: protocol SQI

- ECG một lead, không gọi là MLII; ACC không được dùng làm feature. Waveform trong lượt đã chạy là mirror HF float16 vật lý µV đã pin revision, chuyển float64/mV; không khẳng định bit-identical WFDB gốc.
- Nhãn consensus annotator 4; cửa sổ đủ 10 giây neo theo đầu record, không chồng lấn. Có bất kỳ mẫu consensus 0 thì loại toàn cửa sổ.
- Nhãn worst-class: class 1 = 16.281 cửa sổ, class 2 = 13.748, class 3 = 5.744; có 4.216 cửa sổ mixed. Target QRS usability: class 1+2 usable, class 3 unusable. Phân tích morphology riêng dùng class 1 good, class 2+3 bad.
- 5-fold StratifiedGroupKFold, seed 42, group theo 3 chữ số đầu record (subject). Mọi method dùng cùng folds. Fixed rules giữ ngưỡng sẵn có; learned models, imputer/scaler chỉ fit train fold.
- Preprocess polyphase 1000→500→125 Hz, lấy 1 giây context thật mỗi phía rồi cắt lại; padding ở biên record, không cộng nhiễu và không blanket-denoise.
- Metrics OOF: BA, AUC, F1-good, good/bad recall; coverage = accepted/all, bad accepted = bad/accepted. Báo micro và subject-macro; CI BA bootstrap 2.000 lần theo subject. Ba record được annotate toàn bộ chiếm 88,58% cửa sổ nên đọc cả macro.
- Không dùng nhãn chất lượng này thay annotation R-peak hoặc nhịp AF để chấm supervised BPM/AF. Latency host extraction và learned head được báo riêng; timing C-port ESP32 là thí nghiệm phần cứng khác.

Nguồn: [BUTQDB report](../experiment/report/benchmark_runs/butqdb_20261020/REPORT.md), [protocol](../experiment/report/benchmark_runs/butqdb_20261020/protocol.json).

## 6. LTAFDB: chuyển miền RR và dữ liệu phát triển waveform

- Lượt external RR dùng toàn 84 waveform, kênh 0, 128 Hz, 1.960,6 giờ; giữ model RR đã train AFDB. Không fit lại hoặc tune ngưỡng bằng dữ liệu ngoài; không cộng ma trận nhiễu.
- Target AFIB = dương; nhịp khác đã kiểm duyệt = âm; thời gian trước annotation đầu tiên là unknown. Chấm timeline 1 giây, raw/confirmed và gate Orphanidou là các phân tích riêng.
- Báo BA/Se/Sp/AUC trên phần có output, coverage, AF retention/effective AF recall trên toàn thời gian AF; thêm episode recall, detection delay và false-alert starts/giờ non-AF.
- Lượt train ConvNeXt riêng dùng LTAFDB 72/12 record groups và SHDB-AF 69/24 bệnh nhân train/validation, seed 20261028; lấy tối đa 25 cửa sổ 10 s/lớp/record, loại transition/unknown. Tổng 7.499 cửa sổ, 5.895/1.604 train/validation; train 4 epoch. AFDB không dùng fit ConvNeXt này.
- LTAFDB dùng record ID làm group vì không có patient mapping đã tải. Không tuyên bố patient-disjoint đã xác minh cho mọi record LTAFDB. Không gọi LTAFDB/SHDB-AF là dữ liệu chưa từng dùng đối với ConvNeXt; held-out của fusion và của model base là hai phạm vi riêng.

Nguồn: [external RR report](../experiment/report/benchmark_runs/af_external_20261022/REPORT_CURRENT.md), [protocol ConvNeXt](../experiment/report/benchmark_runs/af_convnext_singlelead_20261028_int8_final/protocol.json), [training inventory](../experiment/report/benchmark_runs/af_convnext_singlelead_20261028_int8_final/training_inventory.csv).

## 7. Những nguồn chỉ nên ghi ở khảo sát bổ sung

- **CinC2011:** khảo sát lịch sử 998 record Set A, 10 s/record, 500 Hz, 12 lead, acceptable/unacceptable toàn recording, 5-fold stratified OOF. Bản single-lead lấy Lead II nhưng nhãn là toàn 12-lead nên không dùng làm bằng chứng xếp hạng SQI single-lead. Artifact lịch sử tương ứng thiếu trong workspace; không đưa vào bảng đánh giá SQI chính hiện hành. Xem [ghi chú đính chính](../experiment/report/REPORT_METRICS.md).
- **LTDB/Fantasia:** mới có đề xuất khảo sát khi có dữ liệu; không liệt kê là đã đánh giá.
- Có thư mục dữ liệu hoặc dataset xuất hiện trong pretraining của checkpoint upstream chưa đủ để ghi là đã benchmark trong dự án.

## Đoạn có thể dùng trong báo cáo

Nghiên cứu đánh giá QRS/RR/BPM trên 57 source record EDB, kết hợp BW/MA/EM từ NSTDB ở các mức cố định và Mixed. Các source record EDB trùng QTDB đã loại khỏi cohort. BUTQDB được dùng đánh giá SQI; mô hình học từ feature chia OOF theo subject, còn fixed rules và C port đóng băng chấm trực tiếp. AFDB so sánh các model AF đóng băng trên record held out; SHDB-AF đánh giá fusion theo split bệnh nhân train/validation/test. Các thí nghiệm có protocol, đơn vị đánh giá và phạm vi độc lập riêng, không gộp accuracy host với chi phí ESP32.
