# Adaptive Pan–Tompkins v2: development-only tuning and external test

## Kết luận

Không chọn v2 để thay v1. V2 được chọn hoàn toàn trên 12 bản ghi development của QTDB, không dùng 70 bản ghi test đã khóa, nhưng không vượt v1 khi đánh giá một lần trên MIT-BIH Arrhythmia Database. Kết quả âm này được giữ nguyên để tránh tối ưu ngược theo test.

## Giao thức

- Development: 12 QTDB records; clean và `bw`, `ma`, `em` ở 0/-6 dB.
- Luật chọn: tối đa `0.6 * macro F1 + 0.4 * worst-condition F1` tại 100 ms; latency là tie-break.
- Cấu hình v2 đã khóa: refractory 260 ms, integration 90 ms, threshold scale 0.38.
- External test: 48 MIT-BIH records, 10 cửa sổ 60 s/record, 480 cửa sổ, 36,389 nhịp chú thích. Không tune sau khi xem kết quả.

## Kết quả external

| Detector | F1 @50 ms | Precision | Recall | F1 @100 ms | BPM MAE | Host latency/window |
|---|---:|---:|---:|---:|---:|---:|
| Pan–Tompkins | 0.9783 | 0.9925 | 0.9646 | 0.9815 | 1.066 bpm | 2.750 ms |
| Adaptive PT v1 | **0.9819** | 0.9896 | **0.9744** | **0.9883** | **0.536 bpm** | 4.507 ms |
| Adaptive PT v2 frozen | 0.9796 | **0.9917** | 0.9679 | 0.9846 | 0.859 bpm | 4.479 ms |

V2 đổi một phần recall lấy precision nhưng tổng F1 và BPM MAE đều kém v1. Vì mục tiêu ưu tiên F1, v1 là ứng viên nghiên cứu tốt hơn; Pan–Tompkins vẫn là baseline nhanh hơn.

Kết quả này không chứng minh v1 là SOTA. Nó chứng minh v1 tổng quát hóa tốt hơn v2 trên external cohort đã chọn. INCART hoặc dữ liệu AD8232 thật vẫn cần thiết trước tuyên bố triển khai.

Nguồn tái lập: `experiments/tune_adaptive_pt_dev.py`, `experiments/evaluate_mitdb_external.py`, `outputs/adaptive_pt_v2_dev/` và `outputs/mitdb_external_qrs/`.
