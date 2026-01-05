import neurokit2 as nk
import numpy as np
import pandas as pd
import os
import random
import matplotlib
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt

FS = 500
DURATION = 10
CHUNK_SIZE = 5000
NUM_SIGNALS = 10
SCALE = 2048

INPUT_NOISE_DIR = r"D:\DOC\Python_ws\ECG\python\output_nstdb"
OUTPUT_ROOT = r"D:\DOC\Python_ws\ECG\python\dataset_real_noise"

np.random.seed(42)
random.seed(42)

def load_full_noise(path):
    return pd.read_csv(path).iloc[:, 0].values

def random_segment(noise, length):
    if len(noise) <= length:
        return np.tile(noise, int(np.ceil(length / len(noise))))[:length]
    start = np.random.randint(0, len(noise) - length)
    return noise[start:start + length]

def ensure_zero_baseline(ecg):
    return ecg - np.mean(ecg)

# load noise
bw_full = load_full_noise(os.path.join(INPUT_NOISE_DIR, "bw.csv"))
ma_full = load_full_noise(os.path.join(INPUT_NOISE_DIR, "ma.csv"))

# tạo thư mục
clean_dir = os.path.join(OUTPUT_ROOT, "clean")
noisy_dir = os.path.join(OUTPUT_ROOT, "noisy")
viz_dir = os.path.join(OUTPUT_ROOT, "visualize")

os.makedirs(clean_dir, exist_ok=True)
os.makedirs(noisy_dir, exist_ok=True)
os.makedirs(viz_dir, exist_ok=True)

t = np.arange(CHUNK_SIZE) / FS

for i in range(NUM_SIGNALS):
    ecg_clean = nk.ecg_simulate(
        duration=DURATION,
        sampling_rate=FS,
        heart_rate=random.uniform(60, 90),
        noise=0
    )[:CHUNK_SIZE]
    
    ecg_clean = ensure_zero_baseline(ecg_clean)
    
    # bốc đoạn ngẫu nhiên từ noise
    bw_noise = random_segment(bw_full, CHUNK_SIZE)
    ma_noise = random_segment(ma_full, CHUNK_SIZE)
    
    noise = bw_noise * random.uniform(1.0, 3.0) + ma_noise * random.uniform(0.5, 2.0)
    ecg_noisy = ecg_clean + noise
    
    # scale x2048 và chuyển int
    ecg_clean_int = (ecg_clean * SCALE).astype(np.int32)
    ecg_noisy_int = (ecg_noisy * SCALE).astype(np.int32)
    
    # lưu từng tín hiệu
    pd.DataFrame({"MLII": ecg_clean_int}).to_csv(
        os.path.join(clean_dir, f"signal_{i:03d}.csv"), index=False
    )
    pd.DataFrame({"MLII": ecg_noisy_int}).to_csv(
        os.path.join(noisy_dir, f"signal_{i:03d}.csv"), index=False
    )
    
    # plot từng tín hiệu (dùng int)
    plt.figure(figsize=(10, 3))
    plt.plot(t, ecg_clean_int, label="Clean", linewidth=1)
    plt.plot(t, ecg_noisy_int, label="Noisy", linewidth=0.8, alpha=0.8)
    plt.title(f"ECG Signal {i:03d}")
    plt.xlabel("Time (s)")
    plt.ylabel("Amplitude (int)")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(viz_dir, f"signal_{i:03d}.png"))
    plt.close()
    
    print(f"✔ Signal {i:03d}")

print("DONE")

# plot ví dụ tín hiệu đầu
ecg_ex = pd.read_csv(os.path.join(clean_dir, "signal_000.csv"))["MLII"].values
noisy_ex = pd.read_csv(os.path.join(noisy_dir, "signal_000.csv"))["MLII"].values

plt.figure(figsize=(12, 4))
plt.plot(ecg_ex[:2000], label="Clean", linewidth=1)
plt.plot(noisy_ex[:2000], label="Noisy", linewidth=1, alpha=0.8)
plt.legend()
plt.xlabel("Sample")
plt.ylabel("Amplitude (int)")
plt.title("Example ECG (scaled x2048)")
plt.grid(True)
plt.tight_layout()
plt.savefig(os.path.join(viz_dir, "example_signal.png"))
plt.show(block=True)