<!-- SQI_GATE_ABLATION_START -->
# Thí nghiệm SQI gate và AF fusion

## Câu hỏi và giao thức

Gate SQI đang có có giúp giảm sai quyết định AF, và mức đánh đổi coverage là bao nhiêu? Chạy retrospective trên cache SHDB-AF đã audit: 19 bệnh nhân test, 1.026 cửa sổ, 1.024 cửa sổ paired cho cả bốn model. Train/validation/test tách bệnh nhân 55/19/19. Giữ nguyên xác suất và ngưỡng đã khóa từ validation; không train, inference mới hoặc chọn ngưỡng trên test.

So sánh không gate với gate `sqi_status == 1` (usable) có sẵn trong firmware Orphanidou + template. Bad/insufficient bị từ chối, không biến thành dự đoán non-AF. SQI dùng trạng thái có sẵn tại thời điểm quyết định, đã kiểm tra không lấy trạng thái tương lai. Không thêm ngưỡng template correlation.

## Kết quả

| Model | Gate | N | Coverage paired % | BA % | F1 AF % | AUC | Lỗi / accepted % | AF giữ lại % |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Device | no_gate | 1024 | 100.00 | 85.45 | 75.37 | 0.9211 | 14.75 | 100.00 |
| Device | device_sqi_usable | 778 | 75.98 | 89.68 | 84.16 | 0.9549 | 7.84 | 71.00 |
| ECGFounder | no_gate | 1024 | 100.00 | 90.00 | 81.44 | 0.9589 | 11.04 | 100.00 |
| ECGFounder | device_sqi_usable | 778 | 75.98 | 92.29 | 87.47 | 0.9712 | 6.30 | 71.00 |
| LR | no_gate | 1024 | 100.00 | 92.02 | 86.16 | 0.9616 | 7.71 | 100.00 |
| LR | device_sqi_usable | 778 | 75.98 | 92.21 | 87.24 | 0.9660 | 6.43 | 71.00 |
| MLP | no_gate | 1024 | 100.00 | 89.05 | 80.94 | 0.9376 | 11.04 | 100.00 |
| MLP | device_sqi_usable | 778 | 75.98 | 90.16 | 82.21 | 0.9531 | 9.51 | 71.00 |

Gate từ chối 246/1.024 cửa sổ (24,02%), gồm 78/269 cửa sổ AF (29,00%). Coverage trên toàn bộ test là 778/1.026 = 75,83%; baseline paired là 99,81%. Không coi cửa sổ bị từ chối là đúng, và không so BA như thể hai tập accepted giống nhau.

| Model | ΔBA (điểm %) | CI 95% bootstrap bệnh nhân (điểm %) | Lỗi trên phần bị từ chối % |
|---|---:|---:|---:|
| Device | +4.23 | [-0.24, +10.54] | 36.59 |
| ECGFounder | +2.29 | [-1.32, +6.56] | 26.02 |
| LR | +0.19 | [-2.17, +2.72] | 11.79 |
| MLP | +1.12 | [-0.85, +3.40] | 15.85 |

2.000 bootstrap paired theo 19 bệnh nhân, seed 42; mỗi replicate lấy bệnh nhân có hoàn lại và giữ tất cả cửa sổ của bệnh nhân đó. Cả bốn CI đều chứa 0: chưa đủ bằng chứng tăng BA ổn định theo bệnh nhân.

## Fusion sửa gì và làm sai gì?

| Baseline → LR fusion | Gate | N | Baseline sai → LR đúng | Baseline đúng → LR sai |
|---|---|---:|---:|---:|
| Device → LR | no_gate | 1024 | 91 | 19 |
| Device → LR | device_sqi_usable | 778 | 26 | 15 |
| ECGFounder → LR | no_gate | 1024 | 43 | 9 |
| ECGFounder → LR | device_sqi_usable | 778 | 2 | 3 |

Trên toàn paired test, LR sửa 43 lỗi ECGFounder nhưng tạo 9 lỗi mới; sau SQI gate chỉ sửa 2 và tạo 3 lỗi mới trên 778 cửa sổ. Vì vậy lợi ích fusion phụ thuộc cohort; không lấy kết quả của một nhánh trên tập khác để khẳng định luôn tốt hơn.

## Diễn giải và giới hạn

- SQI lọc ra một nhóm có tỷ lệ lỗi Device cao, nhưng đồng thời bỏ gần 29% cửa sổ AF. Kết quả này chưa hỗ trợ bật hard gate mặc định cho sàng lọc AF; cần đánh giá cơ chế báo tín hiệu kém/yêu cầu đo lại.
- LR fusion đã nhận feature SQI. Thử nghiệm này loại/bật gate đầu ra, chưa loại feature SQI khỏi model fusion. Muốn đo đóng góp riêng feature SQI cần train lại các biến thể trên train, chọn ngưỡng bằng validation rồi khóa trước test.
- Đây là phân tích chọn lọc đầu ra, không phải bằng chứng SQI khử nhiễu hoặc cải thiện cùng một tín hiệu. Không có nhãn chất lượng độc lập cho thí nghiệm này; provenance pretraining model base giữ các giới hạn của benchmark gốc.
- Đây là replay host từ predictions đã lưu, không đo ESP, latency hoặc vận hành lâm sàng.

![So sánh gate](../visualize/results/AFib/sqi_ablation_20261114/comparison.png)

[Protocol](../outputs/AFib/sqi_ablation_20261114/protocol.json) · [Metrics](../outputs/AFib/sqi_ablation_20261114/metrics.csv) · [Bootstrap contrasts](../outputs/AFib/sqi_ablation_20261114/contrasts.csv) · [Audit](../outputs/AFib/sqi_ablation_20261114/audit.json)

<!-- SQI_GATE_ABLATION_END -->


<!-- SQI_WARNING_START -->
## SQI chỉ cảnh báo: false positive có giảm không?

Dùng cùng 1.024 cửa sổ paired / 19 bệnh nhân test; giữ nguyên model, xác suất và ngưỡng. Mọi dự đoán AF vẫn là AF-positive. SQI chỉ tách thành “usable” và “chất lượng kém, cần xác nhận”; không hạ dự đoán cảnh báo thành non-AF. Không train/inference hoặc chọn ngưỡng mới.

| Model | FP tổng trước → sau | FPR tổng % | FP ở nhánh usable | FP gắn cờ cần xác nhận | TP cũng bị gắn cờ | TP gắn cờ / TP tổng % |
|---|---:|---:|---:|---:|---:|---:|
| Device | 113 → 113 | 14.97 | 32 | 81 | 69 | 29.87 |
| ECGFounder | 92 → 92 | 12.19 | 29 | 63 | 77 | 31.05 |
| LR | 56 → 56 | 7.42 | 30 | 26 | 75 | 30.49 |
| MLP | 84 → 84 | 11.13 | 54 | 30 | 69 | 28.75 |

**Kết quả:** tổng false positive và sensitivity không đổi vì giữ nguyên toàn bộ dự đoán. SQI giúp phân luồng một phần false positive sang cần xác nhận, nhưng cũng chuyển nhiều true positive vào cùng nhánh này. Không gọi số FP ở nhánh usable là FP toàn hệ thống.

| Model | Precision usable % | Precision nhánh cần xác nhận % | Nếu chỉ báo nhánh usable: sensitivity % |
|---|---:|---:|---:|
| Device | 83.51 | 46.00 | 60.22 |
| ECGFounder | 85.50 | 55.00 | 63.57 |
| LR | 85.07 | 74.26 | 63.57 |
| MLP | 76.00 | 69.70 | 63.57 |

Nhánh cảnh báo vẫn chứa AF thật; nếu không xử lý cảnh báo này thì cách triển khai sẽ trở lại hard gate và làm mất phát hiện. Để chứng minh giảm false positive cuối cùng mà giữ sensitivity, cần thử chính sách xác nhận cụ thể (ví dụ thêm nhánh mô hình hoặc thu lại tín hiệu), với quyết định được khóa trước test, và đo lại trên cùng cohort. Cache hiện tại chưa có kết quả đo lại nên chưa thể kết luận chính sách đó hiệu quả.

[Metrics và bootstrap routing](../outputs/AFib/sqi_warning_repro_20261115/metrics.csv) · [Predictions giữ nguyên và nhãn phân luồng](../outputs/AFib/sqi_warning_repro_20261115/routed_predictions.csv) · [Audit](../outputs/AFib/sqi_warning_repro_20261115/audit.json)

<!-- SQI_WARNING_END -->
