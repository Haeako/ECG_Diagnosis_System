import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

# ========== CẤU HÌNH ==========
CSV_FILE = r"D:\DOC\Python_ws\ECG\python\output_nstdb\bw.csv"   # đổi thành ma.csv / ecg_noisy.csv
FS = 500                              # sampling rate
SIGNAL_COLUMN = None                  # None = cột đầu tiên
MAX_FREQ = 100                        # Hz để hiển thị
# ===============================

# ===== 1. LOAD SIGNAL =====
df = pd.read_csv(CSV_FILE)

if SIGNAL_COLUMN:
    signal = df["noise1"].values
else:
    signal = df.iloc[:, 0].values

# signal = signal - np.mean(signal)     # remove DC

N = len(signal)

# ===== 2. FFT =====
fft_vals = np.fft.rfft(signal)
fft_mag = np.abs(fft_vals) / N
freqs = np.fft.rfftfreq(N, d=1/FS)

# ===== 3. PLOT =====
plt.figure(figsize=(10, 4))
plt.semilogy(freqs, fft_mag)
plt.xlim(0, MAX_FREQ)
plt.xlabel("Frequency (Hz)")
plt.ylabel("Magnitude (log)")
plt.title("FFT Spectrum")
plt.grid(True)
plt.tight_layout()
plt.show()
