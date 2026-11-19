# AF từ RR server và giải thích Extra Trees INT8

Đã triển khai và chạy nhánh AF phụ trên server bằng **đúng mô hình Extra Trees INT8 của device**, đóng băng toàn bộ trọng số, lượng tử hóa và runtime RR. Nhánh này cung cấp điểm AF từ RR server và giải thích các đặc trưng RR bằng SHAP. LR fusion đã có vẫn dùng hai score AF cũ; score mới chưa được đưa vào LR.

## Pipeline

```text
ECG một lead → SWT–SepConv gốc → R-peaks server
                                      ↓
                          30 RR liên tục, stride 15
                                      ↓
                         5 đặc trưng bằng C firmware
                                      ↓
                   Extra Trees INT8 của device đã freeze
                           ├→ điểm AF, raw, confirmed 3/4
                           └→ SHAP cho điểm AF thô
```

Đây là cùng bộ phân loại với device, nhưng input RR do SWT–SepConv phát hiện. Điểm AF có thể khác vì vị trí hoặc số đỉnh R khác. Nhánh ECGFounder gốc và pipeline RR/BPM hiện tại vẫn được giữ.

## Model và đặc trưng

Tái dùng nguyên `firmware/esp32/main/components/af_model/af_runtime.c` và `af_model_int8.c`: 80 cây, 17.712 node; RR hợp lệ 250–2.500 ms; 30 RR/cửa sổ, stride 15. RR ngoài miền làm reset lịch sử theo firmware. Ngưỡng raw là score ≥10.240 trên tổng 20.400; confirmed cần bốn cửa sổ và ít nhất ba raw dương.

| Đặc trưng | Cách tính trong runtime gốc |
|---|---|
| pNN50 | Tỷ lệ chênh lệch hai RR liên tiếp lớn hơn 50 ms, lưu trong [0,1] |
| Shannon entropy | Entropy log2 của histogram RR gồm năm bin |
| SD2 | Độ phân tán Poincaré SD2, đơn vị ms |
| SD1/SD2 | Tỷ số hai độ phân tán Poincaré |
| Median RR | Trung vị 30 RR, đơn vị ms |

Input float32 được lượng tử bằng đúng hàm C của firmware. Output được giải thích là điểm truyền đi: `floor(score × 10000 / 20400) / 10000`. Không dùng mô hình Extra Trees float làm mô hình thay thế để giải thích bản INT8.

## Dữ liệu và đồng bộ

Giữ manifest SHDB-AF ECG1 của lượt fusion: 294 đoạn thuộc 98 bản ghi, 93 bệnh nhân, chia 55/19/19 theo bệnh nhân. Mỗi đoạn có 90 s warmup và 180 s đánh giá. Cùng waveform sau AFE/ADC/notch firmware được tái tạo và đối chiếu hash input/ADC với lượt gốc.

Chạy thêm SWT trên chín khối warmup 10 s của mỗi đoạn; 18 khối đánh giá dùng lại peak cache gốc. Peak cục bộ được đổi sang thời gian tuyệt đối trong session và làm tròn mili giây trước khi đi vào runtime C. RR được duy trì qua ranh giới khối; các peak không hợp lệ hoặc trùng sau làm tròn gây dừng, không tự sửa.

Kết quả chỉ sẵn có sau khi khối server hoàn tất, không gán thời điểm có kết quả bằng thời điểm đỉnh R. Replay dùng availability đã lưu và cộng bảo thủ toàn bộ thời gian runtime AF của session vào từng khối. Snapshot vẫn tại cuối khối +1.000 ms. Đây là host replay với network giả lập 0, không phải số đo ESP32 hay truyền thông live.

## Kết quả AF

Nhánh mới có output hợp lệ cho 2.843/3.240 quyết định train, 899/1.026 validation và **908/1.026 test**. Các hàng còn thiếu chưa có cửa sổ AF hợp lệ theo runtime 30 RR, kể cả khi lịch sử bị reset bởi RR ngoài miền. Không biến điểm thiếu thành 0.

Tính trên toàn bộ **908 quyết định test hợp lệ**, 19 bệnh nhân:

| Chỉ số | Kết quả |
|---|---:|
| Coverage trên 1.026 quyết định test | 88,50% |
| Balanced accuracy raw | 85,41% |
| Độ nhạy raw | 90,50% |
| Độ đặc hiệu raw | 80,33% |
| F1 AF raw | 73,99% |
| AUC | 0,9071 |
| TP / TN / FP / FN | 219 / 535 / 131 / 23 |

So sánh trên **cùng 907 quyết định** đủ output của các nhánh:

| Phương pháp | BA | F1 AF |
|---|---:|---:|
| SWT RR → Extra Trees INT8, raw | 85,40% | 73,99% |
| SWT RR → Extra Trees INT8, confirmed 3/4 | 79,69% | 68,67% |
| Device RR → Extra Trees INT8, raw | 85,62% | 74,36% |
| ECGFounder với ngưỡng validation đã khóa | 90,00% | 81,92% |
| LR fusion cũ với ngưỡng validation đã khóa | 92,13% | 86,67% |

Extra Trees dùng ngưỡng firmware cố định; ECGFounder và LR dùng ngưỡng validation đã khóa của lượt trước. Không hiệu chỉnh ngưỡng hoặc train lại model trong lượt này. Nhánh RR server chưa vượt device raw trên cùng hàng; vai trò hiện tại là đối chiếu và giải thích AF dựa trên RR. Các số ở bảng 907 hàng không thay thế bảng fusion gốc trên 1.024 hàng.

## SHAP đã chạy

Tính Shapley theo đủ **32 tập con của năm đặc trưng**, gọi trực tiếp predictor C INT8 cho từng input sau khi thay các đặc trưng thiếu bằng dữ liệu nền. Background gồm **64 cửa sổ train của 55 bệnh nhân**, chọn seed 42 theo vòng từng bệnh nhân, không dùng nhãn hoặc score để chọn. Không dùng bệnh nhân validation/test làm background.

Đã giải thích **782 cửa sổ AF khác nhau**, tương ứng **908 quyết định test**. Một output AF có thể được dùng lại tại nhiều snapshot; biểu đồ global tính theo phân bố 908 quyết định. Với mỗi hàng: `điểm nền + tổng đóng góp SHAP = điểm AF INT8`. Sai số cộng lớn nhất là **2,22 × 10⁻¹⁶**; điểm dự đoán trực tiếp từ năm đặc trưng khớp điểm của runtime RR.

| Đặc trưng | Trung bình trị tuyệt đối SHAP |
|---|---:|
| pNN50 | 0,2396 |
| SD2 | 0,0889 |
| Shannon entropy | 0,0376 |
| Median RR | 0,0290 |
| SD1/SD2 | 0,0123 |

pNN50 có mức đóng góp tuyệt đối trung bình lớn nhất trong tập được giải thích. SHAP dương nâng điểm AF so với điểm nền, SHAP âm hạ điểm AF. Đây là phân rã hành vi của bộ phân loại theo background đã chọn, không phải quan hệ nhân quả sinh lý. Các đặc trưng RR có tương quan; việc trộn từng cột có thể tạo tổ hợp ít gặp. “Exact” áp dụng cho 32 tập con và background hữu hạn này.

### Biểu đồ

![Mức đóng góp tuyệt đối trung bình theo đặc trưng](../visualize/results/experiment/report/benchmark_runs/af_server_rr_xai_20261103_final/xai/global_shap_importance.png)

![Phân bố đóng góp SHAP của 908 quyết định](../visualize/results/experiment/report/benchmark_runs/af_server_rr_xai_20261103_final/xai/shap_beeswarm.png)

![Bốn ví dụ TP TN FP FN và các đóng góp SHAP](../visualize/results/experiment/report/benchmark_runs/af_server_rr_xai_20261103_final/xai/local_shap_top_cases.png)

SHAP này giải thích **điểm AF thô của Extra Trees từ năm đặc trưng RR**. Hình thái ECG, output ECGFounder, xác nhận theo lịch sử 3/4 và quyết định LR cuối cùng cần lớp giải thích tương ứng riêng.

## Mã và artifacts

Để chọn riêng dataset/record và xem lời giải thích theo từng ca, dùng [script chọn dataset và sơ đồ file Python server](SERVER_AF_XAI_RUNNER.md). Script này không chạy lại manifest SHDB cố định của lượt benchmark cũ.

- [Adapter server RR AF](../experiment/src/experiments/af_detection/fusion_sync/server_rr_af.py).
- [C shim gọi runtime và predictor gốc](../experiment/src/experiments/af_detection/fusion_sync/server_rr_af_host.c).
- [Tính SHAP đủ 32 tập con](../experiment/src/experiments/af_detection/exact_rr_shap.py).
- [Runner](../experiment/src/experiments/af_detection/run_server_rr_xai.py).
- [Trạng thái hoàn tất](../experiment/report/benchmark_runs/af_server_rr_xai_20261103_final/status.json).
- [Model và hash](../experiment/report/benchmark_runs/af_server_rr_xai_20261103_final/model.json).
- [Protocol và nguồn cache đã khóa](../experiment/report/benchmark_runs/af_server_rr_xai_20261103_final/protocol.json).
- [Kết quả AF](../experiment/report/benchmark_runs/af_server_rr_xai_20261103_final/metrics.json).
- [Tóm tắt SHAP](../experiment/report/benchmark_runs/af_server_rr_xai_20261103_final/xai/summary.json).
- [Giải thích từng quyết định test](../experiment/report/benchmark_runs/af_server_rr_xai_20261103_final/xai/test_explanations.csv).
- [Background chỉ từ train](../experiment/report/benchmark_runs/af_server_rr_xai_20261103_final/xai/background_train.csv).

Lượt hoàn tất nhập lại các artifact replay đã tính xong, ghi hash của từng tệp và chạy lại phần SHAP/xuất hình. Các model base và LR cũ đều giữ hash trước và sau lượt chạy. Pipeline đọc chính: [device–server và LR fusion](AF_DEVICE_SERVER_FUSION_PIPELINE.md).

Để chạy mới toàn bộ:

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_server_rr_xai.py --output experiment\report\benchmark_runs\af_server_rr_xai_new
```

Tham khảo phương pháp duyệt đủ tập con: [SHAP Exact Explainer](https://shap.readthedocs.io/en/stable/example_notebooks/api_examples/explainers/Exact.html).
