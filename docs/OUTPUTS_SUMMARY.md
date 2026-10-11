# Tổng hợp output hiện tại

Snapshot: **2026-10-03**. Tài liệu này là mục lục kết quả đã sinh trong `outputs/`.
Các con số dưới đây được đọc trực tiếp từ CSV/JSON; không phải kết quả chạy lại.

## 1. Kết luận nhanh

| Khối | Kết quả chính | Trạng thái |
|---|---|---|
| R-peak firmware trên QTDB/MLII | Se 99,66%; PPV 99,87%; F1 99,76% trên 13 record | Tốt trên cohort đã chọn; audit full-record vẫn pending |
| AF model, annotation RR | Extra Trees 5 feature: balanced accuracy 91,58%; F1 89,42%; ROC-AUC 98,17% | Held-out theo record |
| AF model, firmware RR | Balanced accuracy 84,53%; F1 80,73%; ROC-AUC 92,64% | Giảm rõ so với annotation RR |
| AF model INT8 | Agreement float/INT8 99,47%; estimated model 141.881 byte | Khả thi về model; cần đo thật trên ESP32 |
| SQA baseline | Logistic-3SQI risk thấp nhất 6,21% nhưng coverage chỉ 6,24% | Chưa đạt mục tiêu risk ≤5% |
| SQA candidate Extra Trees | Held-out risk 19,69%, coverage 17,31% | Không duyệt deployment |

## 2. R-peak detector firmware

Kết quả đáng dùng nhất hiện tại là bản MLII đã kiểm tra annotation:

- Summary: [`firmware_qtdb_mlii_verified/summary.json`](../outputs/firmware_qtdb_mlii_verified/summary.json)
- Theo record: [`firmware_qtdb_mlii_verified/per_record.csv`](../outputs/firmware_qtdb_mlii_verified/per_record.csv)
- Audit annotation: [`annotation_audit.json`](../outputs/firmware_qtdb_mlii_verified/annotation_audit.json)
- Coverage annotation: [`annotation_coverage.csv`](../outputs/firmware_qtdb_mlii_verified/annotation_coverage.csv)

| Cohort/run | Records | TP | FP | FN | Se | PPV | F1 |
|---|---:|---:|---:|---:|---:|---:|---:|
| MLII verified | 13 | 14.108 | 19 | 48 | 99,66% | 99,87% | 99,76% |
| QTDB polarity-history | 82 | 75.808 | 12.563 | 10.881 | 87,45% | 85,78% | 86,61% |
| QTDB polarity | 82 | 75.607 | 13.228 | 11.082 | 87,22% | 85,11% | 86,15% |
| QTDB baseline | 82 | 75.241 | 13.152 | 11.448 | 86,79% | 85,12% | 85,95% |
| QTDB AC baseline | 82 | 75.638 | 14.060 | 11.051 | 87,25% | 84,33% | 85,76% |

Lưu ý: kết quả 13-record MLII rất cao nhưng là cohort được chọn theo lead. Không
được diễn giải nó như hiệu năng trên toàn bộ 82 record; các summary đều đang ghi
`full_record_coverage_audit_pending: true`.

## 3. AF detection

### 3.1 Dataset và split

- Feature table: [`af_detection/features.csv`](../outputs/af_detection/features.csv)
- Experiment manifest: [`final_benchmark/experiment.json`](../outputs/af_detection/final_benchmark/experiment.json)
- 25 record, 78.921 window; train 18 record/58.727 window, test 7 record/20.194 window.
- Feature được khóa cho benchmark: `pnn50`, `shannon_entropy`, `sd2_ms`,
  `sd1_sd2`, `median_rr_ms`.

### 3.2 Benchmark trên annotation RR

Nguồn chuẩn: [`final_benchmark/metrics.csv`](../outputs/af_detection/final_benchmark/metrics.csv).

| Model | Feature | Balanced accuracy | Sensitivity | Specificity | F1 | ROC-AUC |
|---|---|---:|---:|---:|---:|---:|
| Extra Trees | selected | **91,58%** | **86,76%** | 96,40% | **89,42%** | 98,17% |
| Logistic Regression | selected | 90,22% | 82,51% | **97,94%** | 88,39% | **98,31%** |
| Random Forest | selected | 86,64% | 75,91% | 97,37% | 83,78% | 98,09% |
| XGBoost | selected | 82,44% | 68,13% | 96,76% | 78,00% | 97,65% |

Hình và báo cáo:

- [`model_comparison.png`](../outputs/af_detection/final_benchmark/model_comparison.png)
- [`roc_curves.png`](../outputs/af_detection/final_benchmark/roc_curves.png)
- [`confusion_matrices.png`](../outputs/af_detection/final_benchmark/confusion_matrices.png)
- [`REPORT.md`](../outputs/af_detection/final_benchmark/REPORT.md)

### 3.3 Ảnh hưởng của firmware R-peak/RR

Nguồn chuẩn: [`firmware_rr/screening_metrics.json`](../outputs/af_detection/firmware_rr/screening_metrics.json).
Frozen Extra Trees được đánh giá trên 6 record có waveform; record `00735` bị loại
do không có waveform.

| RR đầu vào | Windows | Balanced accuracy | Sensitivity | Specificity | F1 | ROC-AUC |
|---|---:|---:|---:|---:|---:|---:|
| Annotation RR | 17.525 | 91,20% | 86,72% | 95,68% | 89,51% | 97,83% |
| Firmware RR | 17.868 | 84,53% | 77,87% | 91,19% | 80,73% | 92,64% |
| Firmware RR INT8 | 17.868 | 84,31% | 77,75% | 90,87% | 80,43% | 92,30% |

Firmware R-peak trên cohort này đạt sensitivity 98,22% và PPV 97,11%, nhưng sai
lệch RR vẫn làm AF balanced accuracy giảm **6,67 điểm phần trăm** so với annotation
RR. Đây là suy giảm của toàn pipeline đầu vào, không chứng minh riêng AF classifier
“không tốt”.

### 3.4 INT8 deployment

Nguồn chuẩn: [`int8_deployment/metrics.json`](../outputs/af_detection/int8_deployment/metrics.json).

- Extra Trees: 80 cây, 17.712 node, depth tối đa 8.
- 5 feature INT8; xác suất lá `uint8`.
- Prediction agreement float/INT8: 99,47%.
- Balanced accuracy: float 91,58%, INT8 91,29%.
- Kích thước model ước tính: 141.881 byte; working RAM ước tính: 13 byte.
- Các số tài nguyên là ước tính/host artifact; vẫn cần đo flash, RAM, stack và
  latency thực tế trên ESP32.

## 4. Signal quality assessment (SQA)

### 4.1 So sánh baseline

Nguồn chuẩn:

- [`sqa_baseline_screening/baseline_results.csv`](../outputs/sqa_baseline_screening/baseline_results.csv)
- [`sqa_baseline_screening/metrics.json`](../outputs/sqa_baseline_screening/metrics.json)
- [`sqa_baseline_screening/baseline_comparison.png`](../outputs/sqa_baseline_screening/baseline_comparison.png)

Target `usable`: firmware không FP/FN, có ít nhất hai cặp RR liền kề và RR MAE
≤20 ms. Threshold được chọn bằng 5-fold grouped out-of-fold trên development với
mục tiêu risk ≤5% và tối thiểu 30 window giữ lại.

| Method | ROC-AUC | Balanced accuracy @0.5 | Coverage | Bad among retained |
|---|---:|---:|---:|---:|
| Agreement-only | 82,35% | 56,86% | 0% | Không xác định |
| Logistic-3SQI | 81,38% | 77,63% | 6,24% | **6,21%** |
| ExtraTrees-14SQI | 81,17% | 78,81% | **17,31%** | 19,69% |
| RandomForest-14SQI | 81,06% | **80,13%** | 14,12% | 18,02% |
| Template-rule-inspired | 78,31% | 72,51% | 0% | Không xác định |
| Logistic-14SQI | 74,15% | 74,09% | 8,10% | 19,12% |

Kết luận: chưa baseline nào đạt risk held-out ≤5%. Logistic-3SQI gần nhất nhưng
coverage quá thấp; thêm feature/cây không giải quyết được domain shift.

### 4.2 Candidate deployment quality gate

Nguồn chuẩn: [`ecg_quality_gate_v2/deployment/metrics.json`](../outputs/ecg_quality_gate_v2/deployment/metrics.json).

- Extra Trees: 40 cây, depth tối đa 5, `min_samples_leaf=10`, 14 SQI.
- Calibration: risk 4,97%, coverage 14,37%.
- Held-out test: ROC-AUC 81,17%, risk 19,69%, coverage 17,31%.
- `approved=false`, `firmware_exported=false`.
- File [`candidate_host_only.pkl`](../outputs/ecg_quality_gate_v2/deployment/candidate_host_only.pkl)
  chỉ là candidate để phân tích trên host, không phải model production.

Các file window/diagnostic chính:

- [`ecg_quality_gate_v2/windows.csv`](../outputs/ecg_quality_gate_v2/windows.csv): một dòng mỗi window.
- [`summary.json`](../outputs/ecg_quality_gate_v2/summary.json): tổng hợp development/test.
- [`by_snr.csv`](../outputs/ecg_quality_gate_v2/by_snr.csv): kết quả theo SNR.
- [`feature_diagnostics.csv`](../outputs/ecg_quality_gate_v2/feature_diagnostics.csv): chẩn đoán feature.
- [`threshold_sweep.csv`](../outputs/ecg_quality_gate_v2/threshold_sweep.csv): sweep threshold.
- [`risk_coverage_test.csv`](../outputs/ecg_quality_gate_v2/deployment/risk_coverage_test.csv): đường risk–coverage test.

## 5. Output lịch sử, smoke test và hỗ trợ nghiên cứu

Không dùng các thư mục sau làm số kết luận chính nếu chưa đối chiếu với mục 2–4:

- `outputs/ecg_quality_emg/`: lần chạy quality/EMG trước `ecg_quality_gate_v2`.
- `outputs/ecg_quality_emg_smoke/` và `ecg_quality_emg_sd_smoke/`: smoke test.
- `outputs/firmware_qtdb_baseline/`, `firmware_qtdb_ac_baseline/`,
  `firmware_qtdb_polarity/`, `firmware_qtdb_polarity_history/`: các biến thể lịch
  sử của R-peak benchmark.
- `outputs/research_pubmed_*.json`, `siamaf_tree.json`, `tinyml_*.json`: dữ liệu
  tra cứu/nghiên cứu, không phải metric của model.
- `mplconfig/`, `matplotlib-cache/`, `tmp/`, `ecg_setup_tmp/` và các thư mục
  `pytest*`: cache hoặc artifact tạm, không phải kết quả khoa học.

## 6. File nên dùng theo nhu cầu

| Cần làm gì | File bắt đầu |
|---|---|
| So sánh AF models | `outputs/af_detection/final_benchmark/metrics.csv` |
| Xem split/feature AF | `outputs/af_detection/final_benchmark/experiment.json` |
| Đánh giá AF với firmware RR | `outputs/af_detection/firmware_rr/screening_metrics.json` |
| Kiểm tra INT8 | `outputs/af_detection/int8_deployment/metrics.json` |
| So sánh SQA baselines | `outputs/sqa_baseline_screening/baseline_results.csv` |
| Quyết định deploy quality gate | `outputs/ecg_quality_gate_v2/deployment/metrics.json` |
| Xem R-peak MLII đã audit | `outputs/firmware_qtdb_mlii_verified/summary.json` |

## 7. Trạng thái tổng thể

- **AF classifier:** có baseline held-out tốt trên annotation RR; hiệu năng giảm khi
  dùng RR từ firmware.
- **R-peak detector:** rất tốt trên cohort MLII 13 record, nhưng chưa hoàn tất audit
  độ bao phủ toàn QTDB.
- **Quality assessment:** chưa đạt điều kiện deployment do held-out selective risk
  vượt 5%.
- **AD8232:** hiện chưa có reference; chỉ nên dùng cho pilot, logging và kiểm tra
  domain shift, chưa dùng để báo cáo accuracy lâm sàng.
