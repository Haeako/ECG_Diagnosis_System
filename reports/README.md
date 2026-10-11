# Báo cáo tổng hợp ECG

- Báo cáo LaTeX: `TONG_BAO_CAO_TIEN_DO_ECG.tex`
- Bảng BPM/QRS: `csv/bpm_qrs_results.csv`
- Bảng AF: `csv/af_detection_results.csv`
- Bảng SQI: `csv/sqi_results.csv`
- Bảng XAI: `csv/xai_results.csv`
- Checklist tiến độ: `csv/progress_checklist.csv`

Các CSV dùng UTF-8, dấu phẩy làm delimiter và dấu chấm làm dấu thập phân. Khi mở
trong Excel theo locale Việt Nam, nên dùng **Data > From Text/CSV**, chọn delimiter
`,` và locale/kiểu số dùng dấu chấm để tránh Excel tự đổi số thập phân thành ngày.

Biên dịch báo cáo bằng một trong các lệnh sau (khuyến nghị TeX Live có hỗ trợ tiếng
Việt):

```powershell
pdflatex -interaction=nonstopmode -halt-on-error TONG_BAO_CAO_TIEN_DO_ECG.tex
pdflatex -interaction=nonstopmode -halt-on-error TONG_BAO_CAO_TIEN_DO_ECG.tex
```

Chạy hai lần để cập nhật mục lục và tham chiếu. Máy hiện tại chưa cài trình biên
dịch TeX nên chưa sinh PDF trong workspace.
