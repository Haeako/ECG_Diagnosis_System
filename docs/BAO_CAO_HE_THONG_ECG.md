# Thiết kế hệ thống thu thập điện tâm đồ di động và hỗ trợ chẩn đoán y khoa dựa trên trí tuệ nhân tạo

**Trần Duy Nhân, 23521088**  
**Nguyễn Viết Thiện Nhân, 23521086**  
Khoa Kỹ thuật máy tính, Trường Đại học Công nghệ Thông tin

**Giảng viên hướng dẫn: ThS. Nguyễn Duy Xuân Bách**  
Khoa Kỹ thuật máy tính, Trường Đại học Công nghệ Thông tin

## Tóm tắt

Đề tài nghiên cứu và triển khai hệ thống ECG di động một chuyển đạo sử dụng ESP32, kết hợp xử lý trên thiết bị và máy chủ để hỗ trợ sàng lọc rung nhĩ (AF). Nhánh thiết bị phát hiện đỉnh R, tính khoảng R–R và nhịp tim, sau đó phân loại AF bằng mô hình Extra Trees INT8. Nhánh máy chủ sử dụng SWT–SepConv để phát hiện đỉnh R và ECGFounder một chuyển đạo với mô hình cùng đầu phân loại gốc để lấy điểm AF. Lớp hợp nhất Logistic Regression (LR) học từ điểm hai nhánh, chỉ số chất lượng tín hiệu (SQI), đặc trưng RR/BPM và tuổi kết quả. Trong thử nghiệm phát lại tín hiệu trên máy tính, dữ liệu từ 93 bệnh nhân thuộc tập con SHDB-AF được chia theo bệnh nhân thành 55/19/19 cho huấn luyện, validation và test. Trên cùng 1.024 quyết định test đủ hai nhánh, balanced accuracy của thiết bị, máy chủ và LR fusion lần lượt đạt 85,45%, 90,00% và 92,02%; F1 AF tương ứng là 75,37%, 81,44% và 86,16%. Kết quả cho thấy mức tăng quan sát được khi hợp nhất hai nhánh; khoảng tin cậy bootstrap của chênh lệch so với máy chủ còn chứa 0. Hệ thống đã có nguyên mẫu phần cứng và pipeline được đánh giá bằng host replay; việc kiểm thử toàn tuyến trên ESP32, truyền thông thực tế và đo năng lượng được tiếp tục thực hiện.

**Từ khóa:** ECG một chuyển đạo, ESP32, AD8232, phát hiện QRS, rung nhĩ, SQI, ECGFounder, SWT–SepConv, Logistic Regression, hợp nhất thiết bị và máy chủ.

## 1 Giới thiệu

Phân tích tín hiệu điện tâm đồ là một phương thức phục vụ phát hiện và đánh giá rối loạn nhịp, trong đó có rung nhĩ [1]. Thiết bị ECG di động cho phép thu nhận tín hiệu trong quá trình sử dụng thường ngày, nhưng tín hiệu có thể bị ảnh hưởng bởi chuyển động, tiếp xúc điện cực và đặc tính phần cứng. Các sai lệch này tác động đến việc phát hiện đỉnh R, tính khoảng R–R và kết quả phân loại AF.

Các phương pháp dựa trên khoảng R–R có chi phí tính toán phù hợp với vi điều khiển [2], [4], [6]. Các nghiên cứu và bộ dữ liệu ECG một chuyển đạo cung cấp cơ sở đánh giá AF [3], [5]; các mô hình học sâu trên waveform bổ sung thông tin hình thái tín hiệu [3], [7]. Đề tài lựa chọn kiến trúc phối hợp: thiết bị duy trì xử lý và sàng lọc cục bộ; máy chủ chạy các mô hình có chi phí tính toán lớn hơn; một lớp học máy hợp nhất kết quả cùng thông tin chất lượng và thời điểm sẵn có của từng nhánh.

Đóng góp triển khai của đề tài gồm nguyên mẫu thu nhận ECG một chuyển đạo, pipeline phân tích trên thiết bị và máy chủ, cơ chế ghép output theo thời điểm sẵn có, và lớp LR fusion được đánh giá trên tập test tách bệnh nhân. Phần nghiên cứu tập trung vào mức thay đổi hiệu năng AF khi kết hợp hai nhánh và SQI. Các mô hình server đã phát hành được giữ nguyên trọng số; phần được huấn luyện mới trong lượt fusion là LR.

## 2 Thiết kế và phương pháp

### 2.1 Phần cứng thu nhận ECG

Hệ thống sử dụng ESP32-WROOM-32U để thu nhận tín hiệu ECG một chuyển đạo. Tín hiệu từ điện cực đi qua mạch khuếch đại và lọc tương tự AD8232, sau đó được số hóa bằng ADC của ESP32. Bảng mạch PCB tích hợp khối thu nhận ECG, xử lý trung tâm, quản lý nguồn pin, lưu trữ microSD và truyền thông không dây.

Thiết kế phần cứng hướng tới thu nhận liên tục với tần số lấy mẫu mục tiêu 500 Hz. Trong thử nghiệm fusion được báo cáo dưới đây, nhánh firmware được phát lại ở 360 Hz để giữ đúng cấu hình thuật toán hiện hành; nhánh ECGFounder chuyển đổi cửa sổ sang 500 Hz theo đầu vào mô hình. Các tần số này thuộc các khâu khác nhau và cần được phân biệt khi đánh giá hệ thống.

### 2.2 Nhánh xử lý trên thiết bị

Firmware được phát triển trên ESP-IDF và FreeRTOS, tổ chức các chức năng thu nhận, xử lý tín hiệu, lưu trữ và truyền dữ liệu. Nhánh phân tích giữ nguyên mã C production: lọc notch, phát hiện QRS bằng cấu trúc Elgendi/Two-MA kết hợp tầng quyết định kiểu Pan, sau đó xác định đỉnh R, khoảng R–R và BPM.

Mỗi cửa sổ 30 khoảng RR, dịch 15 khoảng, được chuyển thành năm đặc trưng RR và đưa vào mô hình Extra Trees INT8. Nhánh thiết bị duy trì cả quyết định thô và quyết định xác nhận theo quy tắc 3 trên 4 cửa sổ. Detector và trọng số Extra Trees được đóng băng trong lượt fusion.

SQI theo phương pháp Orphanidou [9] được tính trên cửa sổ 10 giây với bước 1 giây. Trạng thái và điểm SQI được chuyển cho lớp hợp nhất. Trong cấu hình fusion hiện tại, SQI là đặc trưng đầu vào; không áp dụng chặn cứng các đoạn chất lượng thấp trước phân loại.

### 2.3 Nhánh xử lý trên máy chủ

Máy chủ xử lý các cửa sổ ECG 10 giây đã thu nhận đầy đủ qua hai mô hình độc lập.

**Nhánh RR/BPM** sử dụng checkpoint SWT–SepConv và các bước preprocessing, định vị đỉnh của tác giả [8]. Từ đỉnh R, hệ thống tính RR, giữ các khoảng trong miền 250–2.500 ms và tính BPM bằng 60.000 chia trung vị RR theo mili giây khi có ít nhất hai RR hợp lệ. Nhánh này còn cung cấp hệ số biến thiên RR, số đỉnh và chỉ số tương quan mẫu nhịp. Không bổ sung denoiser riêng; các bước DWT và low-pass có sẵn trong preprocessing gốc vẫn được giữ.

**Nhánh AF** sử dụng ECGFounder một chuyển đạo [7], giữ nguyên checkpoint và toàn bộ đầu phân loại 150 nhãn được phát hành. Input được resample về 500 Hz, xử lý bằng bộ lọc gốc và chuẩn hóa z-score trên cửa sổ 10 giây. Việc giới hạn lọc trong từng khối đã thu xong là điều chỉnh khi tích hợp. Hệ thống lưu đủ 150 output sigmoid và lấy nhãn ATRIAL FIBRILLATION ở index 5 làm điểm AF cho fusion. Không thay đầu phân loại bằng một đầu AF tự huấn luyện. Đánh giá trong báo cáo chỉ kiểm chứng nhiệm vụ AF, không suy rộng thành độ chính xác cho 150 nhãn bệnh.

Chỉ số chất lượng phía máy chủ được tính bằng trung vị tương quan giữa từng nhịp và mẫu tạo từ các nhịp còn lại. Chỉ số này được dùng cùng thông tin RR/BPM để hỗ trợ LR phân biệt trường hợp hai nhánh không đồng thuận.

Máy chủ còn có một nhánh AF phục vụ đối chiếu và giải thích: RR từ SWT–SepConv được đưa qua đúng runtime đặc trưng và mô hình Extra Trees INT8 của thiết bị, giữ nguyên trọng số, lượng tử hóa, cửa sổ 30 RR và stride 15. SHAP được tính bằng cách duyệt đủ 32 tập con của năm đặc trưng, gọi trực tiếp predictor INT8 với background gồm 64 cửa sổ train của 55 bệnh nhân. Các đóng góp giải thích điểm AF của nhánh RR này. Score mới chưa được đưa vào LR fusion hiện tại.

### 2.4 Đồng bộ và hợp nhất kết quả

Luồng xử lý tổng thể được xác định như sau:

```text
ECG một chuyển đạo
  ├─ Thiết bị → Elgendi/Two-MA + Pan-head → R-peaks → RR/BPM
  │                                      └→ Extra Trees INT8 → điểm AF
  │          SQI Orphanidou ─────────────────────────────────────┐
  └─ Máy chủ → SWT–SepConv → R-peaks → RR/BPM + template SQI     │
             ECGFounder một lead với đầu gốc → điểm AF           │
                                      ↓                         │
                 Ghép theo session và thời điểm sẵn có ←────────┘
                                      ↓
                 LR fusion → điểm và quyết định AF cuối cùng
```

Mỗi output mang thông tin session, cửa sổ nguồn và thời điểm thực sự sẵn có. Thời điểm đỉnh R đã căn chỉnh không được dùng thay cho thời điểm thuật toán hoàn tất xử lý. Server response được ghép vào snapshot chỉ khi đã có trước hoặc đúng thời điểm quyết định và thuộc đúng khối tín hiệu đang chờ. Output cache của khối trước được nhận diện riêng.

Trong replay, snapshot được lấy tại cuối mỗi khối 10 giây cộng 1.000 ms. Thời điểm server sẵn sàng được mô phỏng bằng thời gian kết thúc khối cộng thời gian preprocess, inference và postprocess đo trên CPU, có tính hàng đợi xử lý tuần tự. Độ trễ mạng được đặt bằng 0. Nguồn waveform được resample offline nên vẫn có lookahead cục bộ nhỏ; đây là thử nghiệm host replay.

LR nhận 13 đặc trưng: hai điểm AF; điểm và trạng thái SQI thiết bị; tuổi output thiết bị; tương quan mẫu nhịp, hệ số biến thiên RR và số đỉnh phía máy chủ; BPM của hai nhánh; chênh lệch BPM tuyệt đối; và mức chênh lệch hai điểm AF. Giá trị thiếu được điền bằng trung vị và các đặc trưng được chuẩn hóa trước khi phân loại. Khi đủ hai nhánh hợp lệ, hệ thống dùng LR; khi thiếu một nhánh, hệ thống dùng nhánh còn lại hợp lệ, với server phải thuộc khối hiện tại. Nếu cả hai thiếu, hệ thống không xuất kết luận.

### 2.5 Dữ liệu và giao thức đánh giá

Thử nghiệm sử dụng ECG1 của tập con SHDB-AF có nhãn đã được lập manifest, gồm 98 bản ghi thuộc 93 bệnh nhân [10]. Seed 42 được dùng để chia theo Subject_ID. Các bệnh nhân không giao nhau giữa ba phần dữ liệu.

| Phần dữ liệu | Bệnh nhân | Bản ghi | Tổng quyết định | Đủ hai nhánh |
|---|---:|---:|---:|---:|
| Huấn luyện | 55 | 60 | 3.240 | 3.229 |
| Validation | 19 | 19 | 1.026 | 1.022 |
| Test | 19 | 19 | 1.026 | 1.024 |
| Tổng | 93 | 98 | 5.292 | 5.275 |

Mỗi bản ghi được lấy ba đoạn tại vị trí 10%, 50% và 90% của miền có thể lấy đoạn, độc lập với nhãn. Mỗi đoạn gồm 90 giây khởi tạo trạng thái thiết bị, 180 giây đánh giá và 2 giây cuối, tạo 18 quyết định. Tổng cộng có 294 đoạn không trùng nhau. Nhãn đích là AF/AFIB tại thời điểm snapshot; các nhịp khác đã được rà soát được xem là non-AF. Các đoạn chuyển nhịp vẫn được giữ.

LR, bộ điền giá trị thiếu và bộ chuẩn hóa chỉ được fit trên 3.229 hàng huấn luyện đủ hai nhánh. LR sử dụng C = 1, class weights cân bằng, solver liblinear và seed 42. Các model base không được huấn luyện lại. Ngưỡng của LR, từng nhánh đơn lẻ và baseline trung bình hai score được chọn riêng trên validation bằng balanced accuracy (BA). Ngưỡng LR cuối cùng là 0,657671. Tập test chỉ dùng để báo kết quả sau khi hoàn tất các lựa chọn này.

So sánh chính sử dụng cùng các hàng test đủ hai nhánh. BA là trung bình của độ nhạy và độ đặc hiệu; F1 được tính cho lớp AF. Độ bao phủ dùng toàn bộ quyết định test làm mẫu số. Độ bất định của chênh lệch BA được ước lượng bằng bootstrap ghép cặp 2.000 lần theo bệnh nhân, giữ toàn bộ các hàng của bệnh nhân được lấy mẫu. Việc tách bệnh nhân áp dụng cho quá trình fit fusion; mức giao nhau với dữ liệu pretraining của mô hình nền chưa được xác định.

## 3 Kết quả và thảo luận

### 3.1 Nguyên mẫu hệ thống

Đề tài đã thiết kế, chế tạo và lắp ráp PCB tích hợp các thành phần chính của hệ thống ECG di động. Thử nghiệm ban đầu cho thấy nguyên mẫu có thể hoạt động bằng pin và thu nhận được dạng sóng ECG. Firmware đã có các thành phần quản lý trạng thái, thu nhận và xử lý tín hiệu, cùng chức năng phục vụ lưu trữ và truyền dữ liệu. Giao diện theo dõi đang tiếp tục được hoàn thiện.

Pipeline device–server và LR fusion đã được nối và chạy bằng host replay. Kết quả dưới đây phản ánh xử lý trên waveform dữ liệu công khai, chưa phải kết quả đánh giá toàn tuyến thu nhận và truyền ECG thực tế từ nguyên mẫu.

### 3.2 Hiệu năng sàng lọc AF

Bảng 1 trình bày kết quả trên cùng 1.024 quyết định của 19 bệnh nhân test, tương ứng độ bao phủ paired 99,81%. Mỗi phương pháp sử dụng ngưỡng đã chọn trên validation.

**Bảng 1 Kết quả AF trên tập test đủ hai nhánh**

| Phương pháp | BA | Độ nhạy | Độ đặc hiệu | F1 AF | AUC |
|---|---:|---:|---:|---:|---:|
| Device Extra Trees INT8 | 85,45% | 85,87% | 85,03% | 75,37% | 0,9211 |
| Server ECGFounder với đầu gốc | 90,00% | 92,19% | 87,81% | 81,44% | 0,9589 |
| Trung bình hai điểm AF | 88,74% | 90,33% | 87,15% | 79,80% | 0,9558 |
| LR chỉ sử dụng hai điểm AF | 90,08% | 91,82% | 88,34% | 81,79% | 0,9643 |
| **LR đầy đủ đặc trưng** | **92,02%** | **91,45%** | **92,58%** | **86,16%** | **0,9616** |

LR đầy đủ đạt 246 true positive, 699 true negative, 56 false positive và 23 false negative. So với thiết bị và máy chủ chạy riêng, BA tăng lần lượt 6,56 và 2,01 điểm phần trăm. So với lấy trung bình hai điểm, BA tăng 3,27 điểm. So với LR chỉ dùng hai điểm AF, việc bổ sung nhóm đặc trưng SQI, tuổi output và RR/BPM tăng 1,93 điểm BA. Thử nghiệm này đánh giá tác động chung của nhóm đặc trưng bổ sung, chưa tách riêng đóng góp của từng chỉ số SQI.

Mức tăng BA của fusion so với server đi cùng độ đặc hiệu tăng từ 87,81% lên 92,58%, trong khi độ nhạy giảm từ 92,19% xuống 91,45%. LR chỉ hai score có AUC cao hơn LR đầy đủ trong lượt này; ưu thế của LR đầy đủ được quan sát ở BA và F1 tại ngưỡng đã chọn bằng validation.

Khi tính cả hai quyết định test thiếu device và dùng fallback server, hệ thống xuất kết luận cho toàn bộ 1.026 quyết định, đạt BA 91,96% và F1 AF 86,01%. Việc báo đồng thời paired coverage và kết quả fallback giúp phân biệt hiệu năng hợp nhất với khả năng cung cấp kết luận của toàn hệ thống.

### 3.3 Giải thích kết quả và độ bất định

Nhánh Extra Trees INT8 chạy trên RR server đã được phát lại trên cùng 294 đoạn, với thêm 90 giây warmup SWT cho mỗi đoạn. Nhánh có output hợp lệ cho 908/1.026 quyết định test, coverage 88,50%, đạt BA raw 85,41% và F1 AF 73,99% tại ngưỡng firmware cố định. SHAP được tính cho toàn bộ 908 quyết định, tương ứng 782 cửa sổ AF khác nhau. Sai số cộng giữa điểm nền, các đóng góp và điểm AF lớn nhất là 2,22 × 10⁻¹⁶. pNN50 có trị tuyệt đối đóng góp trung bình lớn nhất trong tập được giải thích, tiếp theo là SD2. Kết quả này mô tả hành vi phân loại RR so với background đã chọn; không diễn giải thành nguyên nhân sinh lý hoặc lời giải thích cho ECGFounder và LR. Chi tiết và biểu đồ nằm tại [báo cáo nhánh RR server và XAI](AF_SERVER_RR_XAI.md).

Bootstrap 2.000 lần theo 19 bệnh nhân test cho khoảng tin cậy 95% của chênh lệch BA fusion so với server là [−0,03; +4,93] điểm phần trăm. Do khoảng này còn chứa 0, kết quả hiện tại cho thấy mức tăng quan sát được, chưa đủ để kết luận ưu thế thống kê chắc chắn trên quần thể rộng hơn.

Manifest, split bệnh nhân, mã nguồn và model được lưu cùng hash. Audit độc lập đạt 394.914 kiểm tra trên 5.292 hàng, bao gồm kiểm tra ghép theo thời điểm sẵn có, nhãn AF trong đầu gốc 150 output, công thức RR/BPM, thống kê fit của imputer và scaler, dự đoán LR, ngưỡng validation và tái lập bootstrap. Audit sử dụng output đã lưu; không chạy lại inference C hoặc neural và không tính lại tương quan mẫu nhịp từ waveform gốc.

### 3.4 Phạm vi triển khai

Lượt replay và train hoàn tất trong khoảng 593 giây trên host. Thời gian này không được dùng làm latency ESP32, latency mạng hoặc số đo năng lượng thiết bị. Các kết quả AF cũng không phải đánh giá riêng độ chính xác QRS của SWT–SepConv.

Để vận hành toàn tuyến, giao thức truyền cần cung cấp đầy đủ metadata về validity, cửa sổ nguồn và thời điểm output sẵn có. Việc kiểm thử tiếp theo cần dùng waveform thực nhận qua đường truyền, đánh giá mất gói và độ trễ, đo tài nguyên trên ESP32, cũng như đo năng lượng khi hoạt động liên tục. Phần cứng và bộ dữ liệu hiện tại chưa thay thế đánh giá lâm sàng trên người sử dụng.

## 4 Kết luận

Đề tài đã xây dựng nguyên mẫu hệ thống ECG một chuyển đạo và triển khai pipeline phối hợp thiết bị với máy chủ. Thiết bị thực hiện phát hiện QRS, tính RR/BPM và phân loại AF bằng Extra Trees INT8. Máy chủ sử dụng SWT–SepConv cho RR/BPM và ECGFounder một chuyển đạo với đầu phân loại gốc cho điểm AF. LR fusion kết hợp hai điểm với SQI, thông tin RR/BPM và tuổi kết quả để đưa ra quyết định cuối cùng.

Một bản Extra Trees INT8 đóng băng trên máy chủ cũng được chạy bằng RR từ SWT để đối chiếu và giải thích các đặc trưng RR bằng SHAP. Nhánh giải thích này được đánh giá riêng và chưa thay đổi đầu vào của LR fusion.

Trên tập test tách bệnh nhân của thử nghiệm host replay, LR đạt BA 92,02% và F1 AF 86,16%, cao hơn hai nhánh đơn lẻ trên cùng 1.024 quyết định. Khoảng tin cậy chênh lệch BA so với server còn chứa 0, vì vậy cần thêm dữ liệu để xác nhận mức cải thiện. Công việc tiếp theo tập trung hoàn thiện truyền thông và kiểm thử toàn tuyến, đồng thời đo thời gian xử lý, bộ nhớ và năng lượng trên phần cứng. Hệ thống phục vụ nghiên cứu và hỗ trợ sàng lọc AF, không thay thế chẩn đoán y khoa chuyên môn.

## 5 Tài liệu tham khảo

[1] I. C. Van Gelder và cộng sự, “2024 ESC Guidelines for the management of atrial fibrillation developed in collaboration with the European Association for Cardio-Thoracic Surgery (EACTS),” *European Heart Journal*, tập 45, số 36, tr. 3314–3414, 2024. DOI: [10.1093/eurheartj/ehae176](https://doi.org/10.1093/eurheartj/ehae176).

[2] A. Kennedy, D. D. Finlay, D. Guldenring, R. R. Bond, K. Moran và J. McLaughlin, “Automated detection of atrial fibrillation using R-R intervals and multivariate-based classification,” *Journal of Electrocardiology*, tập 49, số 6, tr. 871–876, 2016. DOI: [10.1016/j.jelectrocard.2016.07.033](https://doi.org/10.1016/j.jelectrocard.2016.07.033).

[3] J. Rubin, S. Parvaneh, A. Rahman, B. Conroy và S. Babaeizadeh, “Densely connected convolutional networks for detection of atrial fibrillation from short single-lead ECG recordings,” *Journal of Electrocardiology*, tập 51, số 6S, tr. S18–S21, 2018. DOI: [10.1016/j.jelectrocard.2018.08.008](https://doi.org/10.1016/j.jelectrocard.2018.08.008).

[4] M. Yazid, M. A. Rahman, N. Nuryani và Aripriharta, “Atrial Fibrillation Detection From Electrocardiogram Signal on Low Power Microcontroller,” *IEEE Access*, tập 12, tr. 91590–91604, 2024. DOI: [10.1109/ACCESS.2024.3422329](https://doi.org/10.1109/ACCESS.2024.3422329).

[5] G. D. Clifford và cộng sự, “AF Classification from a Short Single Lead ECG Recording: The PhysioNet/Computing in Cardiology Challenge 2017,” *Computing in Cardiology*, tập 44, tr. 1–4, 2017. DOI: [10.22489/CinC.2017.065-469](https://doi.org/10.22489/CinC.2017.065-469).

[6] G. B. Moody và R. G. Mark, “A new method for detecting atrial fibrillation using R-R intervals,” *Computers in Cardiology*, tập 10, tr. 227–230, 1983. Nguồn: [MIT-BIH Atrial Fibrillation Database](https://physionet.org/content/afdb/1.0.0/).

[7] J. Li và cộng sự, “An Electrocardiogram Foundation Model Built on over 10 Million Recordings,” *NEJM AI*, tập 2, số 7, AIoa2401033, 2025. DOI: [10.1056/AIoa2401033](https://doi.org/10.1056/AIoa2401033). Mã và checkpoint: [ECGFounder](https://github.com/PKUDigitalHealth/ECGFounder).

[8] D. Yun và cộng sự, “Robust R-peak detection in an electrocardiogram with stationary wavelet transformation and separable convolution,” *Scientific Reports*, tập 12, bài 19638, 2022. DOI: [10.1038/s41598-022-19495-9](https://doi.org/10.1038/s41598-022-19495-9). Mã và checkpoint detector: [ECG_peak_detection](https://github.com/dactylogram/ECG_peak_detection).

[9] C. Orphanidou và cộng sự, “Signal-Quality Indices for the Electrocardiogram and Photoplethysmogram: Derivation and Applications to Wireless Monitoring,” *IEEE Journal of Biomedical and Health Informatics*, tập 19, số 3, tr. 832–838, 2015. DOI: [10.1109/JBHI.2014.2338351](https://doi.org/10.1109/JBHI.2014.2338351).

[10] K. Tsutsui và cộng sự, *SHDB-AF*, phiên bản 1.0.1, PhysioNet, 2025. DOI: [10.13026/n6yq-fq90](https://doi.org/10.13026/n6yq-fq90). Bài mô tả dữ liệu: *Scientific Data*, tập 12, bài 454, 2025, DOI: [10.1038/s41597-025-04777-4](https://doi.org/10.1038/s41597-025-04777-4).

## Phụ lục liên kết kết quả tái lập

- [Pipeline triển khai và cấu hình chạy](AF_DEVICE_SERVER_FUSION_PIPELINE.md).
- [Protocol của lượt chạy](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/protocol.json).
- [Kết quả train và đánh giá LR](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/training_summary.json).
- [Model LR đã huấn luyện](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/models/fusion_lr.joblib).
- [Dự đoán từng quyết định](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/models/row_predictions.csv).
- [Audit độc lập](../experiment/report/benchmark_runs/af_fusion_published_swt_20261103/independent_audit.json).
- [Nhánh AF từ RR server và giải thích SHAP](AF_SERVER_RR_XAI.md).
