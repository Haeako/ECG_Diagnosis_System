## AFib

AFDB: tám model, cùng 18.470 cửa sổ thuần nhãn 10s. Model và ngưỡng đóng băng, không train/tune trên AFDB. Fusion LR dùng bản đã train trên 93 người/294 session SHDB chuẩn bị. Chưa xác minh disjoint với toàn bộ pretraining ECGFounder.

| Phương pháp | Sensitivity % | Specificity % | BA % | F1 AF % | AUC |
| :--- | ---: | ---: | ---: | ---: | ---: |
| fusion_lr_full_shdb | 98.6777 | 97.1522 | 97.9150 | 96.3901 | 0.9896 |
| ecgfounder | 96.3553 | 96.2056 | 96.2804 | 94.2620 | 0.9913 |
| extra_trees_float_rr | 88.7778 | 95.9908 | 92.3843 | 89.9828 | 0.9807 |
| extra_trees_int8_rr | 88.5743 | 95.7124 | 92.1433 | 89.5996 | 0.9786 |
| logistic_regression_rr | 83.7769 | 97.5579 | 90.6674 | 88.6616 | 0.9817 |
| random_forest_rr | 77.7589 | 97.3829 | 87.5709 | 84.8266 | 0.9825 |
| xgboost_rr | 69.9610 | 96.7226 | 83.3418 | 79.0765 | 0.9783 |
| convnext | 65.4518 | 84.4722 | 74.9620 | 65.9324 | 0.8705 |
