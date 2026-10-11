# Explainable AF screening: 50 held-out ECG windows

## Kết luận

Mô hình Extra Trees đã khóa được giải thích bằng exact TreeSHAP trên 50 cửa sổ ECG thuộc các bệnh nhân test chưa xuất hiện khi train. Bộ mẫu cố ý chứa cả ca đúng và sai: 15 TP, 15 TN, 10 FP và 10 FN. Vì vậy đây là error analysis, không phải mẫu ngẫu nhiên dùng để ước lượng lại accuracy.

## Thiết kế chống leakage

- Split theo `record_id`: train 18 records, test 7 records.
- Chỉ 6 test records có waveform cục bộ được dùng; record `00735` thiếu file tín hiệu và không bị thay bằng dữ liệu train.
- Mô hình, feature set và threshold không được fit lại khi tạo SHAP.
- 50 mẫu được chọn xác định, round-robin giữa records và cân bằng theo outcome.
- Sai số additivity lớn nhất của SHAP là `2.25e-13`.

## Mô hình đang giải thích

Extra Trees dùng `pnn50`, `shannon_entropy`, `sd2_ms`, `sd1_sd2`, `median_rr_ms`. Trên holdout đầy đủ, mô hình đạt balanced accuracy 0.9158, F1 0.8942 và ROC-AUC 0.9817. Khi RR được tạo bởi detector firmware thay vì annotation, F1 giảm còn 0.8073; sai số QRS phải được báo cáo như một phần của pipeline AF.

## Điều SHAP cho thấy

Global mean absolute SHAP xếp `pnn50` cao nhất (~0.227), sau đó `sd2_ms` (~0.089), `median_rr_ms` (~0.070), `shannon_entropy` (~0.063), và `sd1_sd2` (~0.016). Mô hình chủ yếu học mức bất thường của khoảng RR; đây không phải giải thích nhân quả hay chẩn đoán.

Trong 50 mẫu, xác suất AF trung bình là TP 0.833, TN 0.030, FP 0.749 và FN 0.337. FP có đóng góp dương mạnh từ `pnn50` (+0.220) và `sd2_ms` (+0.100): nhịp không-AF nhưng biến thiên RR cao dễ bị gán AF. FN có `pnn50` (-0.088) và entropy (-0.110) kéo dự đoán xuống. Bốn trong mười FN đến từ record 04936 và bốn từ 07859, cho thấy lỗi tập trung theo bệnh nhân.

## Cách đọc các hình

- `global_shap_importance.png`: độ quan trọng tổng thể, không biểu diễn hướng.
- `shap_beeswarm.png`: độ lớn và hướng tác động.
- `ecg_examples_01_10.png` đến `ecg_examples_41_50.png`: waveform, R peaks, nhãn, dự đoán, xác suất và feature đóng góp.
- `samples_with_shap.csv` và `explanations.json`: dữ liệu kiểm toán từng mẫu.

Luận án nên đặt ít nhất một TP, TN, FP và FN cạnh nhau. SHAP giải thích quyết định của mô hình, không xác nhận cơ chế sinh lý.

Nguồn tái lập: `experiments/af_detection/explain_af_shap.py` và `outputs/af_detection/xai_50/`.
