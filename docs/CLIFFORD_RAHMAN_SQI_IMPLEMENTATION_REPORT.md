# Clifford–Rahman SQI: triển khai và kiểm tra robustness

Ngày chạy: 12/10/2026.

## Phạm vi

Implementation bổ sung nhóm SQI chi phí thấp được mô tả/đánh giá trong:

- Clifford et al. (2012), *Signal quality indices and data fusion for determining
  clinical acceptability of electrocardiograms*, DOI
  [10.1088/0967-3334/33/9/1419](https://doi.org/10.1088/0967-3334/33/9/1419).
- Rahman et al. (2022), *Robustness of electrocardiogram signal quality indices*,
  DOI [10.1098/rsif.2022.0012](https://doi.org/10.1098/rsif.2022.0012).

Đây là **implementation paper-compatible, không phải exact reproduction**:

- `bSQI` dùng mức đồng thuận giữa detector Pan–Tompkins hiện tại và WFDB XQRS;
- nhãn đích là khả năng dùng được cho QRS/RR của hệ thống hiện tại, không phải
  nhãn clinical acceptability do bác sĩ của hai paper;
- fusion dùng SVM và grouped development/test split của dự án;
- benchmark hiện tại dùng cửa sổ 10 giây và muscle artefact từ NSTDB.

## Thành phần đã triển khai

Trong `src/ecg_benchmark/quality.py`:

| Tên | Định nghĩa |
|---|---|
| `clifford_b_sqi` | F1 agreement của hai QRS detector, tolerance 50 ms |
| `clifford_p_sqi` | công suất 5–15 Hz / công suất 0–45 Hz |
| `clifford_k_sqi` | Pearson kurtosis |
| `clifford_s_sqi` | skewness |
| `clifford_bas_sqi` | 1 − công suất 0–1 Hz / công suất 0–40 Hz |
| `rahman_hos_sqi` | abs(skewness) × kurtosis / 5 |
| `rahman_snr_sqi` | var(x) / var(abs(x)), tỷ lệ tuyến tính, không phải dB |

Script `experiments/clifford_rahman_sqi_robustness.py` đánh giá các threshold
tĩnh và SVM fusion. Threshold chọn bằng grouped OOF trên development, với yêu
cầu bad-among-kept không quá 5%, rồi khóa và đánh giá held-out test.

## Protocol đã chạy

- 82 record QTDB, channel 0.
- 30 cửa sổ/record.
- 10 giây/cửa sổ, thêm 2 giây context mỗi phía.
- Original và muscle artefact NSTDB ở 24, 18, 12, 6, 0, −6 dB.
- Tổng cộng 17.220 cửa sổ: 10.500 development và 6.720 test.
- Group split theo source-record, không split ngẫu nhiên theo cửa sổ.
- Target tốt: không FP/FN R-peak, có ít nhất hai RR liền kề và RR MAE ≤20 ms.

## Kết quả

### Threshold tĩnh trên held-out test

| Rule | Balanced accuracy | Precision | Recall | F1 |
|---|---:|---:|---:|---:|
| kSQI > 5 | 0,6911 | 0,6957 | 0,9586 | 0,8063 |
| 0,5 ≤ pSQI ≤ 0,8 | 0,5218 | 0,6772 | 0,1267 | 0,2135 |
| abs(sSQI) ≤ 0,8 | 0,3598 | 0,2779 | 0,1090 | 0,1566 |
| snrSQI > 10 | 0,5000 | 0 | 0 | 0 |

Kết quả xác nhận cảnh báo của Rahman: threshold tĩnh không chuyển tốt sang
dataset/định nghĩa chất lượng mới. Đặc biệt không được hiểu `snrSQI` là SNR dB.

### Adaptive SVM fusion

| Split | ROC-AUC | Balanced accuracy | Precision | Recall | F1 |
|---|---:|---:|---:|---:|---:|
| Development grouped OOF | 0,8177 | 0,7957 | 0,7844 | 0,8674 | 0,8238 |
| Held-out test | **0,8554** | **0,8008** | **0,8010** | **0,9136** | **0,8536** |

Tuy nhiên threshold selective-risk học ở development là `0,963465`. Trên test:

- kept: **0/6.720**;
- coverage: **0%**;
- risk: không xác định;
- firmware export: **không được duyệt**.

Model phân loại có AUC/F1 khá nhưng xác suất không transfer đủ tốt để thỏa ràng
buộc risk 5%. Vì thế không được dùng model này làm gate cứng cho AF.

### Robustness đáng chú ý

- AUC theo SNR: −6 dB `0,9328`; 0 dB `0,8926`; 6 dB `0,7867`; original
  `0,7666`. AUC cao ở nhiễu nặng một phần vì hai lớp dễ tách hơn, không có nghĩa
  tín hiệu −6 dB tốt hơn tín hiệu sạch.
- AUC theo source database dao động từ `0,8143` (European ST-T) đến `0,9870`
  (MIT-BIH Normal Sinus Rhythm).
- F1 theo source database dao động từ `0,6318` đến `0,9888`.

Dao động này là bằng chứng trực tiếp về dataset dependence mà Rahman mô tả.

## Quyết định kỹ thuật

1. Giữ các SQI mới để telemetry, phân tầng lỗi và làm input cho gate thế hệ sau.
2. Không dùng threshold tĩnh của paper để chặn AF.
3. Không export SVM hiện tại xuống firmware vì coverage held-out bằng 0% ở risk 5%.
4. Bước tiếp theo phải bổ sung electrode-motion và baseline-wander, đánh giá nhiều
   window size (1/2/5/10 giây), rồi calibrate theo thiết bị AD8232 thật.
5. Nếu mục tiêu vẫn là bảo vệ AF downstream, chọn threshold bằng AF-risk/coverage,
   không chỉ bằng nhãn “waveform sạch”.

## Artifact

- Feature implementation: `src/ecg_benchmark/quality.py`
- Unit tests: `tests/test_quality.py`
- Robustness runner: `experiments/clifford_rahman_sqi_robustness.py`
- Full windows: `outputs/ecg_quality_clifford_rahman_v1/windows.csv`
- Metrics: `outputs/clifford_rahman_sqi_v1/metrics.json`
- Robustness breakdown: `outputs/clifford_rahman_sqi_v1/robustness.csv`

