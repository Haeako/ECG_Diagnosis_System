# Pipeline SQA-based ECG waveform delineation cho wearable AD8232–ESP32

## 1. Mục tiêu và phạm vi

Mục tiêu của pipeline là không cố delineate mọi đoạn ECG. Hệ thống đánh giá trước
đoạn tín hiệu có phù hợp với tác vụ nào, sau đó mới chạy nhánh phân tích tương ứng:

| Mức chất lượng | Khả năng được phép | Đầu ra |
|---|---|---|
| Q2 — morphology quality | P–QRS–T nhìn thấy và biên có thể xác định | P/QRS/T peak, onset, offset; PR, QRS, QT/QTc |
| Q1 — rhythm quality | QRS còn xác định tin cậy nhưng P/T hoặc biên không tin cậy | R peak, RR, HR và AF screening |
| Q0 — unusable | QRS không tin cậy | Không suy luận; yêu cầu gắn lại điện cực/đo lại |

Cách chia này bám sát ý tưởng của Xie et al. [1] và ontology ba lớp của BUT QDB.
Theo [5], chất lượng phải được định nghĩa theo **tác vụ downstream**, không phải chỉ theo
mức nhiễu nhìn thấy.

## 2. Khảo sát ngắn

Xie et al. [1] xử lý các đoạn wearable ECG dài 4 s, lọc band-pass, tính năm chỉ số
`adSQI1`, `adSQI2`, `ptSQI`, `rHR`, `tHR`, sau đó dùng neural network phân loại
good/acceptable/unacceptable. R và/hoặc T được nhận dạng tùy kết quả SQA. Nghiên
cứu dùng 7.133 đoạn gán nhãn thủ công từ 29 người, báo cáo accuracy SQA 96,74%,
độ chính xác nhận dạng R và T lần lượt 99,95% và 99,57% trên các lớp được phép.
Các con số này thuộc cohort riêng của nghiên cứu, không được chuyển thành claim
cho AD8232 nếu chưa kiểm định lại.

Orphanidou et al. [2] cho thấy một SQI wearable có thể kết hợp kiểm tra miền sinh
lý, khoảng RR và tương quan QRS-template. Công trình báo cáo ECG sensitivity 94%
và specificity 97% trên dữ liệu gán nhãn thủ công, nhưng cũng nhấn mạnh SQI phụ
thuộc thiết bị và bối cảnh. Andreotti et al. [3] dùng nhiều QRS detector và bSQI
(độ đồng thuận detector), cùng các đặc trưng HRV, morphology và SQI. Kết quả pilot
của repo này cho thấy agreement đơn lẻ không đủ; do đó agreement chỉ là một
feature, không phải ground truth.

Với delineation, wavelet delineator của Martínez et al. [4] là baseline kinh điển,
được đánh giá trên các database chuẩn. QTDB [6] cung cấp expert fiducial annotations
cho P, QRS và T. So sánh gần đây trên smartwatch cho thấy hiệu năng phụ thuộc thiết
bị và fiducial point; ECGdeli ổn định nhất tổng thể trong các toolkit được so sánh
[9]. Điều này củng cố yêu cầu phải đánh giá ngoài miền và trên thiết bị mục tiêu.

## 3. Pipeline đề xuất

```text
AD8232 @ 360 Hz
      |
      v
[Kiểm tra acquisition]
timestamp gap / ADC clipping / flatline / lead-off
      |
      v
[Hai nhánh lọc causal]
QRS: 5–18 Hz hiện có       Morphology/SQI: 0.5–40 Hz + notch 50 Hz
      |                                  |
      +------------> cửa sổ 4 s, hop 2 s
                                      |
                                      v
                         [R candidates + T candidates]
                                      |
                                      v
                         [14 SQI + rHR + tHR/ptSQI]
                                      |
                                      v
                       [Quality model: Q2 / Q1 / Q0]
                         /             |             \
                       Q2              Q1             Q0
                       |               |              |
               P-QRS-T delineator   R/RR/HR only   reject + feedback
                       |               |
                       +------- physiological checks
                                      |
                                      v
                      fiducials + confidence + quality provenance
```

### 3.1 Acquisition guards

Chạy trên block ngắn trước mọi model:

1. kiểm tra timestamp liên tục và đúng cadence 360 Hz;
2. phát hiện ADC saturation/clipping;
3. phát hiện flatline hoặc lead-off;
4. kiểm tra biên độ ngoài miền đã định trước từ dữ liệu board;
5. nếu guard fail, trả Q0 ngay và không nội suy qua gap.

Các rule này có ưu điểm rẻ, giải thích được và bắt được lỗi phần cứng mà model SQI
không nên phải học.

### 3.2 Tiền xử lý

- Dùng filter causal và lưu rõ group delay trong firmware.
- Giữ nhánh QRS 5–18 Hz hiện tại để tương thích detector firmware.
- Thêm nhánh morphology 0,5–40 Hz; không dùng high-pass 5 Hz cho đo P/T/ST vì có
  nguy cơ làm mất thành phần tần số thấp và dịch chuyển biên.
- Notch 50 Hz chỉ bật khi kiểm định cho thấy có lợi; đánh giá clean-signal damage
  bằng protocol CleanGuard hiện có.
- Khi benchmark offline, không dùng `filtfilt` để tuyên bố độ trễ realtime.

### 3.3 Cửa sổ và candidate waves

- Cửa sổ SQA: 4 s, overlap 50% theo [1].
- Detector R chạy với context hai phía; chỉ chấm điểm core để tránh boundary effect.
- T-wave candidate tìm trong vùng tương đối sau R; P-wave candidate tìm trước QRS.
- Với nhịp quá chậm khiến 4 s thiếu beat, trả `insufficient_data`, không ép class.

### 3.4 Feature vector SQA

Nhóm tối thiểu có thể triển khai:

1. acquisition: clipping ratio, flatline ratio;
2. spectral: baseline-power ratio, high-frequency-power ratio, spectral entropy;
3. statistical: normalized derivative RMS, kurtosis;
4. detector: bSQI/agreement, beat-count disagreement, peak-timing disagreement;
5. morphology: median QRS-template correlation, QRS-amplitude CV;
6. rhythm: RR-outlier fraction, rHR;
7. T-related: ptSQI và tHR theo thiết kế của [1], sau khi tái hiện công thức từ
   full text hoặc mô tả lại rõ đây là implementation tương đương, không phải bản
   tái hiện nguyên gốc.

Không dùng SNR, annotation hoặc injected-noise level làm input feature. Không dùng
ngưỡng HR cứng như bằng chứng duy nhất về chất lượng vì arrhythmia thật có thể bị
nhầm thành artifact [7].

### 3.5 Quality model

Baseline chính cho ESP32:

- ordinal logistic regression;
- shallow Extra Trees: 20–40 cây, depth 3–5;
- so sánh với rule-based Orphanidou-style [2].

Output là ba xác suất `P(Q2), P(Q1), P(Q0)`. Quyết định theo chi phí bất đối xứng:
nhầm Q0 thành Q2 nguy hiểm hơn nhầm Q2 thành Q1. Threshold được chọn trên
calibration records và khóa trước test. Nếu uncertainty cao, hạ xuống mức thận
trọng hơn (`Q2 -> Q1 -> Q0`).

### 3.6 Delineation có điều kiện

- **Q2:** chạy wavelet delineator [4] hoặc ECGdeli [8] làm baseline nghiên cứu;
  phiên bản embedded dùng multiscale derivative/wavelet lifting và local search
  quanh R để tìm P/QRS/T onset, peak, offset.
- **Q1:** chỉ xuất R peaks/RR/HR và cho phép AF model hiện có chạy; không xuất QT,
  PR hoặc morphology.
- **Q0:** không xuất clinical measurement; lưu reason code và yêu cầu đo lại.

Mỗi fiducial phải kèm confidence và provenance: quality class, cửa sổ, filter
version, model version. Physiological interval checks chỉ là postcondition để
flag/reject, không tự sửa fiducial nhằm làm kết quả “đẹp”.

## 4. Dữ liệu và thiết kế thí nghiệm

### 4.1 Dữ liệu

| Vai trò | Dataset | Cách dùng |
|---|---|---|
| Quality chính | BUT QDB [5] | 3 lớp tương ứng waveform/QRS/unusable; có free-living ECG và accelerometer |
| Fiducial chính | QTDB [6] | train/validate P/QRS/T onset–offset và timing error |
| Noise stress | NSTDB [10] | thêm baseline wander, muscle artifact, electrode-motion noise ở các mức SNR khóa trước |
| Rhythm robustness | AFDB | kiểm tra SQA không loại có hệ thống AF/irregular rhythm; firmware-RR end-to-end |
| Device pilot | AD8232 SD 360 Hz | kiểm tra cadence, range và phản ứng theo hoạt động; chưa claim accuracy khi chưa có reference |

### 4.2 Split chống leakage

1. Split theo subject/record trước khi tạo cửa sổ.
2. Tất cả overlap windows và noise variants từ cùng clean window phải ở cùng split.
3. Noise recording cũng chia theo time block trước khi resample/mix.
4. Feature selection, imputation, normalization và threshold chỉ fit trên train/dev.
5. Giữ một test set untouched cho mỗi database; thêm external test giữa database.

### 4.3 Nhãn

- Quality label phải mô tả tác vụ: Q2 nếu expert có thể đặt P/QRS/T boundaries;
  Q1 nếu QRS/RR tin cậy nhưng P/T boundaries không tin cậy; Q0 nếu QRS không tin cậy.
- QTDB dùng annotation để tạo nhãn downstream: delineation đạt tolerance đã đăng ký
  trước hay không.
- AFDB dùng annotation-RR và frozen AF output để đo gate có bảo vệ đúng pipeline AF.
- AD8232 chưa có reference chỉ dùng pilot; sau đó cần dual recording hoặc expert
  annotation để trở thành validation set.

## 5. Evaluation protocol

### 5.1 SQA

Báo cáo confusion matrix, macro-F1, balanced accuracy, one-vs-rest sensitivity và
specificity cho Q2/Q1/Q0. Quan trọng hơn, báo cáo:

- coverage của Q2 và Q1+Q2;
- `failure-to-reject`: tỷ lệ cửa sổ delineation/RR sai vẫn được gate nhận;
- risk–coverage curve và 95% CI bootstrap theo subject/record;
- kết quả theo activity, noise type, SNR, rhythm và database.

Không chọn model bằng accuracy đơn lẻ vì class imbalance và chi phí lỗi khác nhau.

### 5.2 Delineation

Cho từng `P_on`, `P_peak`, `P_off`, `QRS_on`, `R`, `QRS_off`, `T_peak`, `T_off`:

- sensitivity và positive predictivity;
- timing error trung bình, bias, SD, median và IQR (ms);
- gross-error/missed-wave rate;
- PR, QRS, QT/QTc interval error;
- các metric trên toàn bộ test và trên phần được gate nhận.

Đánh giá end-to-end phải tính cửa sổ bị reject vào coverage; không chỉ báo cáo độ
chính xác trên subset dễ đã được nhận. QTDB là reference chính cho fiducials [6].

### 5.3 Ablation bắt buộc

1. delineator không gate vs gate Q2/Q1/Q0;
2. agreement-only vs full SQI;
3. rule-based vs logistic vs shallow trees;
4. clean vs từng noise type/SNR;
5. annotation R vs firmware R;
6. float vs INT8;
7. public dataset vs AD8232.

## 6. Lộ trình triển khai trong repo

### Phase A — reproducible offline baseline

1. Thêm adapter BUT QDB và mapping ba nhãn.
2. Tạo bảng một dòng/cửa sổ gồm SQI, label, subject/record và activity.
3. Train ba baseline với grouped split; khóa thresholds theo failure-to-reject.
4. Chạy wavelet/ECGdeli trên QTDB và tạo bảng fiducial-level.
5. Ghép SQA + delineation và xuất risk–coverage/timing report.

### Phase B — firmware equivalence

1. Chỉ giữ feature có C/Python parity.
2. Export model INT8 và test vectors như AF model hiện có.
3. Đo flash, RAM, stack, p50/p95 latency trên ESP32.
4. Replay cùng waveform qua Python và production C; yêu cầu quyết định Q0/Q1/Q2
   và fiducial indices khớp trong tolerance đã đăng ký.

### Phase C — AD8232 pilot và validation

1. Thu SD full-rate ở các trạng thái ngồi yên, co cơ, đi bộ, lay dây, tháo/gắn lại
   điện cực; ghi event timestamps.
2. Dùng pilot để phát hiện scale/cadence/domain shift, không tuning bằng test labels.
3. Thu dual-recording hoặc expert annotations cho một cohort riêng.
4. Freeze pipeline rồi báo cáo external device performance và failure-to-reject.

## 7. Tiêu chí go/no-go cho firmware

Không export quality gate chỉ vì ROC-AUC cao. Chỉ go khi:

1. threshold khóa từ development đạt giới hạn failure-to-reject trên held-out;
2. performance được báo cáo cùng coverage và confidence interval;
3. không có subgroup rhythm/activity bị giảm nghiêm trọng chưa giải thích;
4. C/Python parity đạt yêu cầu;
5. flash/RAM/latency đo trên ESP32 đạt budget;
6. output luôn mang trạng thái `insufficient quality`, không biến Q0 thành chẩn đoán.

Candidate 14-SQI hiện tại trong repo không đạt điều kiện 1 (held-out risk 19,69%
so với giới hạn 5%), vì vậy đúng quyết định là giữ host-only và dùng nó như baseline
âm, không deploy.

## Tài liệu tham khảo

DOI paper và dataset đã được chuẩn hóa tại [DOI ledger](DOI_LEDGER.md).

[1] J. Xie et al., “A signal quality assessment-based ECG waveform delineation
method used for wearable monitoring systems,” *Medical & Biological Engineering
& Computing*, 59(10), 2073–2084, 2021. DOI: 10.1007/s11517-021-02425-8.

[2] C. Orphanidou et al., “Signal-quality indices for the electrocardiogram and
photoplethysmogram: Derivation and applications to wireless monitoring,” *IEEE
Journal of Biomedical and Health Informatics*, 19(3), 832–838, 2015.
DOI: 10.1109/JBHI.2014.2338351.

[3] F. Andreotti et al., “Comparing feature-based classifiers and convolutional
neural networks to detect arrhythmia from short segments of ECG,” *Computing in
Cardiology*, 44, 2017. DOI: 10.22489/CinC.2017.360-239.

[4] J. P. Martínez et al., “A wavelet-based ECG delineator: Evaluation on standard
databases,” *IEEE Transactions on Biomedical Engineering*, 51(4), 570–581, 2004.
DOI: 10.1109/TBME.2003.821031.

[5] A. Nemcova et al., “Brno University of Technology ECG Quality Database
(BUT QDB), version 1.0.0,” PhysioNet, 2020. DOI: 10.13026/kah4-0w24.

[6] P. Laguna et al., “A database for evaluation of algorithms for measurement of
QT and other waveform intervals in the ECG,” *Computers in Cardiology*, 24,
673–676, 1997. DOI: 10.1109/CIC.1997.648140.

[7] C. Daluwatte et al., “Assessing ECG signal quality indices to discriminate ECGs
with artefacts from pathologically different arrhythmic ECGs,” *Physiological
Measurement*, 37(8), 1370–1382, 2016. DOI: 10.1088/0967-3334/37/8/1370.

[8] S. Pilia et al., “ECGdeli — An open source ECG delineation toolbox for MATLAB,”
*SoftwareX*, 13, 100639, 2021. DOI: 10.1016/j.softx.2020.100639.

[9] K. M. Jaeger et al., “Systematic comparison of ECG delineation algorithm
performance on smartwatch data,” *IEEE Access*, 12, 160794–160804, 2024.
DOI: 10.1109/ACCESS.2024.3487013.

[10] G. B. Moody, W. K. Muldrow, and R. G. Mark, “A noise stress test for
arrhythmia detectors,” *Computers in Cardiology*, 11, 381–384, 1984; MIT-BIH
Noise Stress Test Database, PhysioNet.
