# Screening phương pháp và baseline ECG Signal Quality Assessment

## Kết luận chính

Sáu baseline đã được chạy trên cùng protocol QTDB + NSTDB, với split theo
source-record group. Không baseline nào đạt điều kiện deployment đã đăng ký trước
(`bad among retained <= 5%` trên held-out test). Baseline gần nhất là
**Logistic-3SQI**, giữ 6,24% test với risk 6,21%. Agreement-only có ROC-AUC cao
nhất (0,8235) nhưng không tìm được threshold calibration khả thi. Vì vậy kết quả
hiện tại là một baseline screening âm có giá trị, không phải quality gate sẵn sàng
cho firmware.

## 1. Screening phương pháp

| Phương pháp | Đầu vào/chủ ý chính | Output | Giá trị làm baseline | Lưu ý tái lập |
|---|---|---|---|---|
| Xie et al. 2021 [1] | `adSQI1`, `adSQI2`, `ptSQI`, `rHR`, `tHR`; neural network | good / acceptable / unacceptable; nhận R và/hoặc T tùy quality | Baseline gần nhất với đề tài SQA-based delineation | Dataset 29 người và công thức chi tiết không công khai đầy đủ trong abstract; không tuyên bố đã reproduce |
| Orphanidou et al. 2015 [2] | HR range, RR consistency, adaptive QRS-template correlation | good/bad cho reliable HR | Rule-based, nhẹ, phù hợp wearable | Screening dùng bản `Template-rule-inspired`, không phải bản sao chính xác |
| Smital et al. 2020 [3] | SNR curve trong local window | full-wave / QRS-only / unusable | Đúng ontology ba mức cần cho delineation có điều kiện | Công trình báo cáo ngưỡng 18 dB cho full-wave và 5 dB cho QRS trên protocol riêng; không chuyển thẳng sang AD8232 |
| Andreotti et al. 2017 [4] | 4 QRS detectors, bSQI, HRV, morphology, SQI; bagged trees + MLP | N/A/O/noise | Cơ sở cho detector agreement và morphology SQI | 169 features và nhiều detector quá nặng cho ESP32; noise label của CinC 2017 ít |
| Agreement-only | F1 agreement Pan–Tompkins/XQRS | usability score | Sanity baseline tối giản | Hai detector có thể cùng sai |
| Logistic-3SQI | agreement, HF power ratio, normalized derivative RMS | xác suất RR usable | Baseline nhỏ nhất có khả năng export | Gần đạt risk gate nhưng coverage rất thấp |
| Logistic/RF/ExtraTrees-14SQI | detector, spectral, statistical, QRS-template, RR SQIs | xác suất RR usable | Kiểm tra lợi ích feature/model phức tạp hơn | Không generalize đủ tốt sang held-out source groups |

## 2. Screening dataset

| Dataset | Nội dung/nhãn | Vai trò phù hợp | Trạng thái trong repo |
|---|---|---|---|
| BUT QDB [5] | 18 long-term single-lead recordings từ 15 người, free-living, ECG 1000 Hz + accelerometer; quality lớp 1/2/3 | Dataset chính để học full-wave / QRS-only / unusable | Chưa tải; archive 2,2 GB, cần phase riêng và subject split |
| QTDB [6] | 105 đoạn hai kênh, 15 phút, 250 Hz; 3.622 beats có expert P/QRS/T fiducials | Delineation reference và nguồn clean waveform | Đã có local; SQA run dùng 82 records có reference phù hợp |
| NSTDB [7] | baseline wander, muscle artifact, electrode-motion noise; các mức 24 đến −6 dB | Noise stress có paired clean/reference | Đã có `ma`; baseline hiện tại mới kiểm tra muscle artifact |
| CinC 2017 [8] | 8.528 single-lead recordings 9–60 s, 300 Hz; N/A/O/noise | External short wearable ECG; kiểm tra arrhythmia-vs-noise | Chưa tải cho run này; lớp noise chỉ 284 records và không đồng nghĩa task-defined quality |
| AFDB | 25 long recordings, rhythm annotations; đa số có waveform/QRS | Kiểm tra quality gate không loại AF thật và ảnh hưởng firmware-RR | Đã có local; chưa dùng làm quality-label run này |
| AD8232 | SD full-rate 360 Hz | Device pilot/external validation | Chưa có reference; chỉ được dùng inspection cho tới khi có dual recording/expert labels |

Các database không có cùng định nghĩa nhãn. Do đó không được gộp `noise`, SNR và
“RR usable” thành một target duy nhất mà không công bố mapping theo tác vụ.

## 3. Protocol baseline đã chạy

- Nguồn ECG: QTDB channel 0, 82 records.
- Noise: NSTDB muscle artifact `ma`, chia time block trước resampling.
- Cửa sổ: 10 s core + 2 s context hai phía.
- Điều kiện: original, 24, 18, 12, 6, 0 và −6 dB; tổng 17.220 cửa sổ.
- Development: 10.498 cửa sổ / 48 source groups.
- Held-out test: 6.720 cửa sổ / 32 source groups.
- Target `usable`: firmware detector không FP/FN, có ít nhất hai adjacent RR pairs,
  RR MAE <= 20 ms.
- Learned baselines: 5-fold GroupKFold out-of-fold trên development.
- Threshold: coverage lớn nhất với `bad among retained <= 5%`, tối thiểu 30 cửa sổ.
- Không dùng test labels để chọn feature, model hoặc threshold.

## 4. Kết quả rerun

| Method | ROC-AUC | Average precision | Balanced accuracy @0.5 | Threshold | Coverage | Bad among retained |
|---|---:|---:|---:|---:|---:|---:|
| Agreement-only | **0,8235** | 0,7970 | 0,5686 | không khả thi | 0% | — |
| Logistic-3SQI | 0,8138 | **0,8089** | 0,7763 | 0,8703 | 6,24% | **6,21%** |
| ExtraTrees-14SQI | 0,8117 | 0,7941 | 0,7881 | 0,7686 | 17,31% | 19,69% |
| RandomForest-14SQI | 0,8106 | 0,7901 | **0,8013** | 0,9128 | 14,12% | 18,02% |
| Template-rule-inspired | 0,7831 | 0,7776 | 0,7251 | không khả thi | 0% | — |
| Logistic-14SQI | 0,7415 | 0,7286 | 0,7409 | 0,9495 | 8,10% | 19,12% |

`Balanced accuracy @0.5` chỉ đo khả năng phân lớp ở ngưỡng mặc định. Quyết định
deployment dựa trên risk–coverage với threshold khóa từ development. Vì thế
Random Forest có balanced accuracy cao nhất nhưng không phải gate tốt nhất.

![Baseline comparison](../outputs/sqa_baseline_screening/baseline_comparison.png)

## 5. Diễn giải

1. Agreement có ích để **ranking** nhưng không đủ làm gate: không threshold nào
   trên development vừa giữ >=30 cửa sổ vừa đạt risk <=5%.
2. Logistic-3SQI chuyển miền tốt hơn model 14 features. Thêm feature/cây làm tăng
   coverage calibration nhưng risk held-out tăng mạnh, dấu hiệu source/domain shift.
3. Template-rule không đạt vì regular morphology/HR không bảo đảm detector đúng;
   ngược lại arrhythmia thật có thể trông “không đều” nhưng không phải artifact [9].
4. QTDB+synthetic muscle noise là benchmark detector/RR, chưa phải validation cho
   P/T delineation hoặc wearable free-living quality.
5. Bước dữ liệu có giá trị nhất tiếp theo là BUT QDB với subject-level split, sau
   đó external test trên AD8232 có reference. Tăng depth/số cây hiện chưa được hỗ trợ.

## 6. Artifact tái lập

- Runner: `experiments/sqa_baseline_screening.py`
- Input: `outputs/ecg_quality_gate_v2/windows.csv`
- Bảng máy đọc: `outputs/sqa_baseline_screening/baseline_results.csv`
- Manifest/metrics: `outputs/sqa_baseline_screening/metrics.json`
- Hình: `outputs/sqa_baseline_screening/baseline_comparison.png`

Lệnh:

```powershell
$env:MPLCONFIGDIR='outputs\sqa_baseline_screening\mplconfig'
.\.venv\Scripts\python.exe experiments\sqa_baseline_screening.py `
  --windows outputs\ecg_quality_gate_v2\windows.csv `
  --output outputs\sqa_baseline_screening
```

## Tài liệu tham khảo

DOI paper và dataset đã được chuẩn hóa tại [DOI ledger](DOI_LEDGER.md).

[1] J. Xie et al., “A signal quality assessment-based ECG waveform delineation
method used for wearable monitoring systems,” *Medical & Biological Engineering
& Computing*, 59(10), 2073–2084, 2021. DOI: 10.1007/s11517-021-02425-8.

[2] C. Orphanidou et al., “Signal-quality indices for the electrocardiogram and
photoplethysmogram: Derivation and applications to wireless monitoring,” *IEEE
JBHI*, 19(3), 832–838, 2015. DOI: 10.1109/JBHI.2014.2338351.

[3] L. Smital et al., “Real-time quality assessment of long-term ECG signals
recorded by wearables in free-living conditions,” *IEEE TBME*, 67(10),
2721–2734, 2020. DOI: 10.1109/TBME.2020.2969719.

[4] F. Andreotti et al., “Comparing feature-based classifiers and convolutional
neural networks to detect arrhythmia from short segments of ECG,” *Computing in
Cardiology*, 44, 2017. DOI: 10.22489/CinC.2017.360-239.

[5] A. Nemcova et al., “Brno University of Technology ECG Quality Database
(BUT QDB), version 1.0.0,” PhysioNet, 2020. DOI: 10.13026/kah4-0w24.

[6] P. Laguna et al., “A database for evaluation of algorithms for measurement of
QT and other waveform intervals in the ECG,” *Computers in Cardiology*, 24,
673–676, 1997. DOI: 10.1109/CIC.1997.648140.

[7] G. B. Moody, W. K. Muldrow, and R. G. Mark, “A noise stress test for
arrhythmia detectors,” *Computers in Cardiology*, 11, 381–384, 1984; PhysioNet
NSTDB DOI: 10.13026/C2HS3T.

[8] G. D. Clifford et al., “AF classification from a short single lead ECG
recording: the PhysioNet/Computing in Cardiology Challenge 2017,” *Computing in
Cardiology*, 44, 2017. DOI: 10.22489/CinC.2017.065-469.

[9] C. Daluwatte et al., “Assessing ECG signal quality indices to discriminate
ECGs with artefacts from pathologically different arrhythmic ECGs,”
*Physiological Measurement*, 37(8), 1370–1382, 2016.
DOI: 10.1088/0967-3334/37/8/1370.
