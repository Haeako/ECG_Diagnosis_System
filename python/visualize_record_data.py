import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# ================= CONFIG =================
INPUT_FOLDER = r"D:\DOC\Python_ws\ECG\python\examination_ecg\examination_ecg"
OUTPUT_FOLDER = "ecg_plots"
FS = 500  # Hz

os.makedirs(OUTPUT_FOLDER, exist_ok=True)

print("📂 Reading ECG files...")

# ================= PROCESS =================
for file in sorted(os.listdir(INPUT_FOLDER)):
    if not file.endswith(".csv"):
        continue

    file_path = os.path.join(INPUT_FOLDER, file)
    df = pd.read_csv(file_path)

    gt = df["MLII"].values
    # noisy = df["filtered"].values
    t = np.arange(len(gt)) / FS

    # ========== PLOT ==========
    # plt.figure(figsize=(16, 5))

    plt.plot(t, gt, label="Ground Truth", linewidth=1.2)
    # plt.plot(t, noisy, label="Filtered Signal", alpha=0.8)

    plt.xlabel("Time (s)")
    plt.ylabel("Amplitude")
    plt.title(f"ECG Visualization – {file}", fontsize=11)
    plt.legend(fontsize=9)
    plt.grid(alpha=0.3)

    # 👉 Layout thủ công, không warning
    # plt.subplots_adjust(
    #     left=0.06,
    #     right=0.98,
    #     top=0.88,
    #     bottom=0.15
    # )

    # ========== SAVE ==========
    out_name = file.replace(".csv", ".png")
    out_path = os.path.join(OUTPUT_FOLDER, out_name)
    plt.savefig(out_path, dpi=200)
    plt.close()

    print(f"✅ Saved: {out_name}")

print("\n🎉 Done! All ECG plots saved.")
