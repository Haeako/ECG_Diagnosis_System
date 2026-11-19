# Python experiment modules

Nhánh `feature/experiment` chỉ chạy chuẩn bị dữ liệu, benchmark các module, tính metrics và tạo số liệu/plot cho báo cáo. Không khôi phục pipeline production.

Mỗi domain BPM, SQI và AFib có đúng `prepare_dataset.py`, `eval.py`, `metrics.py` cùng `modules/`. Mỗi method là một module riêng. Không thêm launcher, watcher hoặc report runner riêng. `server/AF` giữ tên thư mục để dùng tiếp venv hiện có; output dùng `AFib`.

- `eval.py --report`: tái tạo đủ cohort đã chốt từ cache đã audit, kiểm tra metrics khớp rồi cập nhật báo cáo và plot. Không suy luận hoặc train.
- `eval.py --from-cache`: chỉ xuất metrics/plot từ cache cho lựa chọn hiện tại.
- `eval.py` với manifest: chạy module. RR/fusion AF dùng feature input đã audit; hai model waveform chạy inference mới. Đây là benchmark module, không mặc định chạy lại cả pipeline.
- `--limit`, `--records`, `--conditions`: lượt kiểm tra subset; không dùng với `--report`.
- `AF/eval.py --train-fusion`: train LR bằng SHDB, cutoff từ OOF tách người. Không train/tune trên AFDB và không dùng với `--report`.

Mỗi lệnh lưu `metrics.csv`, predictions, `protocol.json`, `status.json`; plot nằm ở `visualize/results/<domain>/<tên output>/`. Output phải là thư mục mới. Python được chọn rõ qua `server/<domain>/.venv/Scripts/python.exe`; không tự fallback môi trường.

Xem [lệnh chạy từ đầu](../README.md) và hướng dẫn [BPM](BPM/README.md), [SQI](SQI/README.md), [AFib](AF/README.md). Dataset, checkpoint và cache không được Git tải tự động. Các repo clone giữ source độc lập và được ignore.
