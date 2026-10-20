# Báo cáo tổng hợp — BPM/RR và AF đã khôi phục

Cập nhật 19/10/2026. Lượt mới **không bổ sung hoặc chạy thêm SQI**.

**Báo cáo chính:** [REPORT_BPM_AF.md](benchmark_runs/non_sqi_20261019/REPORT_BPM_AF.md).

| Phạm vi | Đã tổng hợp/chạy lại |
|---|---|
| BPM/QRS lịch sử | 21 tên detector, 20 run có per-window, 397 dòng metric; không trộn protocol |
| BPM/RR hiện tại | Replay C 13 QTDB MLII; 2.327 mốc BPM; 14.054 cặp RR liên tiếp matched |
| AF | Logistic, Random Forest, Extra Trees, XGBoost × all15/selected5; thêm INT8 C |
| AF waveform | 6 record test × clean / NSTDB MA burst 12 / 6 / 0 dB; không gate |
| AF runtime | Raw và confirmed 3/4; cả 4 điều kiện |
| Kết quả AF mới | 62 dòng metric; predictions từng cửa sổ và bảng suy hao riêng |
| AF/gate lịch sử | Giữ đủ 116 dòng đã có, không chạy thêm SQI, không gọi biến thể cũ là NeuroKit |
| Chi phí | p50/p99 host đo mới; pickle bytes; số Xtensa/ESP32 cũ tách riêng, không đánh tráo host với hardware |

BPM display: MAE **1,8104 BPM**, RMSE **6,0744 BPM**, **94,97%** mốc sai số ≤5 BPM, coverage **100%**. RR MAE **4,0348 ms** trên cặp liên tiếp matched, coverage reference RR **99,37%**. QRS F1 **99,7667%** không phải accuracy BPM.

8/8 cấu hình AF float tái hiện **chính xác** metric annotation-holdout gốc, không fit lại. Đã tái hiện lỗi uint32 overflow trong builder AF firmware-RR cũ và chạy lại bằng timestamp an toàn từ source hiện tại. Không dùng số AF firmware-RR cũ bị ảnh hưởng làm kết quả hiện hành.

- [Tất cả AF metrics mới](benchmark_runs/non_sqi_20261019/af_metrics.csv)
- [Suy hao từng mô hình](benchmark_runs/non_sqi_20261019/af_degradation.csv)
- [Toàn bộ BPM/QRS lịch sử](benchmark_runs/non_sqi_20261019/bpm_all_historical_metrics.csv)
- [Chi phí AF host](benchmark_runs/non_sqi_20261019/af_host_cost.csv)
- [Số mẫu/nhãn](benchmark_runs/non_sqi_20261019/dataset_counts.csv)
- [Checklist và giới hạn](benchmark_runs/non_sqi_20261019/REPORT_BPM_AF.md#7-giới-hạn-và-checklist-còn-lại)

## CinC2011 — artifact cũ, ngoài phạm vi lượt mới

**Giới hạn đã đính chính:** CinC2011 không có nhãn chất lượng riêng cho Lead II. Các hàng single-lead bên dưới chỉ dự đoán nhãn toàn bản ghi từ Lead II, **không dùng để kết luận/ranking SQI single-lead**. Giữ nguyên số lịch sử để truy vết; không phải kết quả mới trong lượt này.

Hoàn thành ngày 19/10/2026 trong 301,26 giây. Artifact: [metrics.csv](benchmark_runs/cinc2011_20261019/metrics.csv), [predictions.csv](benchmark_runs/cinc2011_20261019/predictions.csv), [manifest.json](benchmark_runs/cinc2011_20261019/manifest.json).

Dataset: 998 record Set A có nhãn (773 acceptable, 225 unacceptable), 10 giây/record, 500 Hz. Không trộn nhiễu nhân tạo. Nhãn thuộc toàn bộ bản ghi 12 lead; input single-lead dùng II. Không chạy AF downstream.

Gate cố định chạy trực tiếp trên waveform; learned gate dùng cùng 5-fold stratified OOF, seed 42. Không tuning trên test fold. AUC SVM dùng decision_function. Bad accepted = số record unacceptable được nhận / tổng record được nhận.

Zhao[NeuroKit]: gọi trực tiếp NeuroKit 0.2.13, zhao2018/simple. Ho[NeuroKit]: gọi trực tiếp ho2025 với UNSW+NeuroKit; score là tỷ lệ mẫu có quality=1, không phải tỷ lệ số RR. Các ngưỡng 80/90/100% là policy ở cấp record.

Orphanidou: rules + mean-template CC, EP Limited tại 125 Hz. Kramer: ECGAssess rules tại 500 Hz. bSQI single-lead: F1 agreement EP Limited/WQRS, 50 ms. Clifford-84: 7 SQI × 12 leads từ implementation public hiện có, EP Limited warm-up hai lượt và WQRS Python, 150 ms; tên này không có nghĩa là đã khớp số liệu private test của paper.

## baseline

| Method | BA | AUC | Coverage | Bad accepted | 95% CI BA |
|---|---:|---:|---:|---:|---|
| No gate | 0.5000 | 0.5000 | 100.00% | 22.55% | [0.5000, 0.5000] |

## fixed

| Method | BA | AUC | Coverage | Bad accepted | 95% CI BA |
|---|---:|---:|---:|---:|---|
| Orphanidou | 0.7456 | 0.7738 | 74.05% | 10.96% | [0.7109, 0.7802] |
| Kramer[ECGAssess] | 0.7336 | 0.7367 | 85.07% | 12.96% | [0.7009, 0.7670] |
| Zhao[NeuroKit] | 0.7248 | 0.7676 | 80.16% | 12.75% | [0.6898, 0.7596] |
| bSQI F1 >=0.8 | 0.6463 | 0.7213 | 44.89% | 11.16% | [0.6115, 0.6793] |
| Ho[NeuroKit] >=80% | 0.7385 | 0.7860 | 72.95% | 11.13% | [0.7032, 0.7739] |
| Ho[NeuroKit] >=90% | 0.7363 | 0.7860 | 65.93% | 10.03% | [0.7022, 0.7704] |
| Ho[NeuroKit] >=100% | 0.7166 | 0.7860 | 56.21% | 9.09% | [0.6836, 0.7503] |

## single-feature learned

| Method | BA | AUC | Coverage | Bad accepted | 95% CI BA |
|---|---:|---:|---:|---:|---|
| bSQI Logistic | 0.6925 | 0.7192 | 68.04% | 12.67% | [0.6572, 0.7274] |
| bSQI RBF-SVM | 0.6792 | 0.7238 | 66.43% | 13.12% | [0.6429, 0.7134] |
| psqi Logistic | 0.5559 | 0.5961 | 63.33% | 19.46% | [0.5180, 0.5920] |
| psqi RBF-SVM | 0.6339 | 0.6285 | 93.19% | 17.53% | [0.6037, 0.6615] |
| ssqi Logistic | 0.5960 | 0.5730 | 82.87% | 18.50% | [0.5622, 0.6267] |
| ssqi RBF-SVM | 0.6335 | 0.6757 | 92.69% | 17.51% | [0.6036, 0.6615] |
| ksqi Logistic | 0.6358 | 0.5807 | 93.49% | 17.47% | [0.6060, 0.6632] |
| ksqi RBF-SVM | 0.6349 | 0.6864 | 93.79% | 17.52% | [0.6054, 0.6622] |
| fsqi Logistic | 0.5301 | 0.5874 | 45.99% | 20.26% | [0.4916, 0.5654] |
| fsqi RBF-SVM | 0.6991 | 0.7452 | 85.07% | 14.37% | [0.6639, 0.7315] |
| bassqi Logistic | 0.5422 | 0.5455 | 73.65% | 20.54% | [0.5090, 0.5760] |
| bassqi RBF-SVM | 0.5555 | 0.5217 | 89.48% | 20.38% | [0.5286, 0.5831] |

## project feature fusion

| Method | BA | AUC | Coverage | Bad accepted | 95% CI BA |
|---|---:|---:|---:|---:|---|
| Project b+p+k+bas Logistic | 0.7006 | 0.7612 | 73.75% | 13.04% | [0.6652, 0.7354] |
| Project b+p+k+bas RBF-SVM | 0.6947 | 0.7471 | 82.16% | 14.27% | [0.6608, 0.7284] |
| Clifford-5 feature subset Logistic | 0.6823 | 0.7502 | 72.24% | 13.73% | [0.6449, 0.7168] |
| Clifford-5 feature subset RBF-SVM | 0.7280 | 0.7861 | 79.76% | 12.56% | [0.6922, 0.7607] |
| Clifford-6 single-lead subset Logistic | 0.6911 | 0.7581 | 74.05% | 13.53% | [0.6556, 0.7257] |
| Clifford-6 single-lead subset RBF-SVM | 0.7456 | 0.7998 | 84.27% | 12.37% | [0.7131, 0.7773] |
| Project-5SQI Logistic | 0.7118 | 0.7885 | 77.25% | 12.97% | [0.6762, 0.7456] |

## project score fusion

| Method | BA | AUC | Coverage | Bad accepted | 95% CI BA |
|---|---:|---:|---:|---:|---|
| bSQI + Orphanidou → Logistic | 0.7427 | 0.7822 | 73.15% | 10.96% | [0.7097, 0.7765] |
| bSQI + Orphanidou → RBF-SVM | 0.7351 | 0.7521 | 77.76% | 11.98% | [0.7013, 0.7687] |
| bSQI + Zhao[NeuroKit] → Logistic | 0.7365 | 0.7886 | 68.64% | 10.51% | [0.7016, 0.7700] |
| bSQI + Zhao[NeuroKit] → RBF-SVM | 0.7320 | 0.7660 | 71.94% | 11.28% | [0.6954, 0.7661] |
| bSQI + Kramer → Logistic | 0.7144 | 0.7649 | 81.66% | 13.37% | [0.6805, 0.7483] |
| bSQI + Kramer → RBF-SVM | 0.7343 | 0.7758 | 86.07% | 13.04% | [0.7016, 0.7683] |
| Orphanidou + Zhao[NeuroKit] → Logistic | 0.7404 | 0.7991 | 69.24% | 10.42% | [0.7070, 0.7747] |
| Orphanidou + Zhao[NeuroKit] → RBF-SVM | 0.7502 | 0.7566 | 78.76% | 11.45% | [0.7162, 0.7858] |
| Orphanidou + Kramer → Logistic | 0.7463 | 0.7779 | 74.15% | 10.95% | [0.7120, 0.7805] |
| Orphanidou + Kramer → RBF-SVM | 0.7487 | 0.7856 | 75.85% | 11.10% | [0.7147, 0.7837] |
| Zhao[NeuroKit] + Kramer → Logistic | 0.7453 | 0.7849 | 77.56% | 11.50% | [0.7099, 0.7801] |
| Zhao[NeuroKit] + Kramer → RBF-SVM | 0.7453 | 0.7395 | 77.56% | 11.50% | [0.7099, 0.7801] |

## project stacked fusion

| Method | BA | AUC | Coverage | Bad accepted | 95% CI BA |
|---|---:|---:|---:|---:|---|
| bSQI + Logistic-score | 0.7226 | 0.7882 | 73.15% | 11.92% | [0.6871, 0.7566] |
| All-five-score fusion | 0.7455 | 0.8054 | 76.25% | 11.30% | [0.7123, 0.7809] |

## project cascade

| Method | BA | AUC | Coverage | Bad accepted | 95% CI BA |
|---|---:|---:|---:|---:|---|
| bSQI → Zhao[NeuroKit] cascade | 0.5064 | 0.5071 | 4.11% | 17.07% | [0.4931, 0.5192] |

## multilead reference

| Method | BA | AUC | Coverage | Bad accepted | 95% CI BA |
|---|---:|---:|---:|---:|---|
| Clifford-84 Logistic | 0.8996 | 0.9510 | 74.35% | 3.77% | [0.8751, 0.9227] |
| Clifford-84 RBF-SVM | 0.9031 | 0.9535 | 74.45% | 3.63% | [0.8802, 0.9254] |
| Clifford-84 MLP | 0.8556 | 0.9321 | 77.76% | 6.57% | [0.8274, 0.8842] |

## Tái lập

`features.csv`: toàn bộ feature tính lại; `predictions.csv`: từng record/method/fold; `metrics.csv`: metric và cấu hình; `manifest.json`: version, hash và protocol. Mọi fusion trong bảng là experiment của dự án. Không gọi b+p+k+bas Logistic/SVM là phương pháp Zhao gốc.

