import pandas as pd
import os

# ===== CẤU HÌNH =====
INPUT_FILE = r"D:\DOC\Python_ws\ECG\python\test\100_with_ma.csv"
OUTPUT_ROOT = r"D:\DOC\Python_ws\ECG\python\output_mitdb\chunks"
CHUNK_SIZE = 5000

SIGNAL_COLUMN = None   # None = lấy toàn bộ cột, hoặc ghi tên cột
SCALE_UP = 2048
# ====================

# lấy tên record (100)
record_name = os.path.splitext(os.path.basename(INPUT_FILE))[0]

# tạo folder cho record
record_folder = os.path.join(OUTPUT_ROOT, record_name)
os.makedirs(record_folder, exist_ok=True)

# đọc csv
df = pd.read_csv(INPUT_FILE)

if SIGNAL_COLUMN:
    df = df[[SIGNAL_COLUMN]]

num_samples = len(df)
num_chunks = num_samples // CHUNK_SIZE

print(f"Total samples: {num_samples}")
print(f"Chunks: {num_chunks}")

ADC_BITS = 12
ADC_MAX = (1 << ADC_BITS) - 1
ADC_MID = ADC_MAX // 2

for i in range(num_chunks):
    start = i * CHUNK_SIZE
    end = start + CHUNK_SIZE

    chunk = df.iloc[start:end].copy()

    # scale [-1,1] → ADC
    chunk = (chunk + 1.0) * ADC_MID
    chunk = chunk.round().astype(int)

    chunk.insert(0, "n", range(CHUNK_SIZE))

    out_name = f"{record_name}_{i:03d}.csv"
    out_path = os.path.join(record_folder, out_name)
    chunk.to_csv(out_path, index=False)

    print(f"Saved: {out_path}")


print("DONE ✅")
