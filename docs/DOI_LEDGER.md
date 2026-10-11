# DOI ledger cho ECG signal quality và delineation

Cập nhật: 2026-10-01. Bảng này chỉ ghi các tài liệu/dataset đã thực sự được dùng
trong survey, protocol hoặc baseline của repo. DOI được chuẩn hóa thành URL phân
giải được; nguồn kiểm chứng ưu tiên PubMed, PhysioNet và trang nhà xuất bản.

| ID | Loại | Tài liệu / dataset | DOI | Dùng trong repo | Trạng thái |
|---|---|---|---|---|---|
| P01 | Paper | Xie et al. (2021), SQA-based ECG delineation | [10.1007/s11517-021-02425-8](https://doi.org/10.1007/s11517-021-02425-8) | Khung SQA + delineation | Đã kiểm chứng qua PubMed |
| P02 | Paper | Orphanidou et al. (2015), ECG/PPG SQI | [10.1109/JBHI.2014.2338351](https://doi.org/10.1109/JBHI.2014.2338351) | SQI và quality gate | Đã kiểm chứng qua PubMed |
| P03 | Paper | Smital et al. (2020), wearable ECG quality | [10.1109/TBME.2020.2969719](https://doi.org/10.1109/TBME.2020.2969719) | SQA trong free-living/wearable | Đã kiểm chứng qua PubMed |
| P04 | Paper | Andreotti et al. (2017), short-ECG classifiers | [10.22489/CinC.2017.360-239](https://doi.org/10.22489/CinC.2017.360-239) | Baseline feature-based | Đã kiểm chứng qua CinC |
| P05 | Paper | Martínez et al. (2004), wavelet ECG delineator | [10.1109/TBME.2003.821031](https://doi.org/10.1109/TBME.2003.821031) | Benchmark delineation | Đã kiểm chứng qua DOI |
| P06 | Paper | Laguna et al. (1997), QT Database | [10.1109/CIC.1997.648140](https://doi.org/10.1109/CIC.1997.648140) | QTDB và fiducial benchmark | Đã kiểm chứng qua IEEE/PhysioNet |
| P07 | Paper | Daluwatte et al. (2016), SQI vs arrhythmia | [10.1088/0967-3334/37/8/1370](https://doi.org/10.1088/0967-3334/37/8/1370) | Cảnh báo rhythm–quality confounding | Đã kiểm chứng qua PubMed |
| P08 | Paper | Pilia et al. (2021), ECGdeli | [10.1016/j.softx.2020.100639](https://doi.org/10.1016/j.softx.2020.100639) | Delineation baseline | Đã kiểm chứng qua DOI |
| P09 | Paper | Jaeger et al. (2024), smartwatch delineation | [10.1109/ACCESS.2024.3487013](https://doi.org/10.1109/ACCESS.2024.3487013) | External wearable context | Đã kiểm chứng qua DOI |
| P10 | Paper | Clifford et al. (2017), AF Challenge | [10.22489/CinC.2017.065-469](https://doi.org/10.22489/CinC.2017.065-469) | CinC 2017 / AF classification | Đã kiểm chứng qua PMC/CinC |
| D01 | Dataset | BUT QDB v1.0.0 | [10.13026/kah4-0w24](https://doi.org/10.13026/kah4-0w24) | Quality-label benchmark | Đã kiểm chứng qua PhysioNet |
| D02 | Dataset | QT Database v1.0.0 | [10.13026/C24K53](https://doi.org/10.13026/C24K53) | Fiducial/delineation benchmark | Đã kiểm chứng qua PhysioNet |
| D03 | Dataset | MIT-BIH Noise Stress Test Database v1.0.0 | [10.13026/C2HS3T](https://doi.org/10.13026/C2HS3T) | Noise injection/stress test | Đã kiểm chứng qua PhysioNet |
| D04 | Dataset | MIT-BIH Atrial Fibrillation Database v1.0.0 | [10.13026/C2MW2D](https://doi.org/10.13026/C2MW2D) | AFDB/domain comparison | Đã kiểm chứng qua PhysioNet |

## Quy ước trích dẫn

- Khi dùng dữ liệu, trích cả paper mô tả (nếu có) và DOI của phiên bản dataset.
- CinC 2017 không có DOI dataset độc lập được xác nhận trong lần rà soát này;
  dùng paper P10, không tự tạo DOI.
- Paper gốc của Moody, Muldrow và Mark (1984) về noise stress test chưa tìm thấy
  DOI đáng tin cậy; khi dùng NSTDB, trích DOI dataset D03.
- Với dữ liệu AD8232 tự thu chưa có publication/reference, ghi provenance nội bộ
  (thiết bị, firmware, sampling rate, ngày thu, subject/session), không gán DOI.

## Nguồn kiểm chứng chính

- [PubMed: Xie et al.](https://pubmed.ncbi.nlm.nih.gov/34432182/)
- [PubMed: Orphanidou et al.](https://pubmed.ncbi.nlm.nih.gov/25069129/)
- [PubMed: Smital et al.](https://pubmed.ncbi.nlm.nih.gov/31995473/)
- [PhysioNet: BUT QDB](https://physionet.org/content/butqdb/1.0.0/)
- [PhysioNet: QTDB](https://physionet.org/content/qtdb/1.0.0/)
- [PhysioNet: NSTDB](https://physionet.org/content/nstdb/1.0.0/)
- [PhysioNet: AFDB](https://physionet.org/content/afdb/1.0.0/)
- [PMC: Clifford et al.](https://pmc.ncbi.nlm.nih.gov/articles/PMC5978770/)
- [PubMed: Daluwatte et al.](https://pubmed.ncbi.nlm.nih.gov/27454007/)

