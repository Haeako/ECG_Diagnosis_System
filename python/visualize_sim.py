import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
import os
from glob import glob
from sklearn.metrics import mean_squared_error

# ==================== CONFIG ====================
FOLDER_FILT = r"D:\DOC\Python_ws\ECG\python\filtered_out_real_0_05"
# FOLDER_FILT = r"D:\DOC\Python_ws\ECG\python\filtered_out_real_0_2"
# FOLDER_GT = r"D:\DOC\Python_ws\ECG\python\dataset_real_noise\clean"
FOLDER_GT = r"D:\DOC\Python_ws\ECG\python\examination_ecg\examination_ecg"
OUTPUT_PLOT_DIR = r"D:\DOC\Python_ws\ECG\python\evaluation_plots_sim_0_05_real"

FS = 500 

if not os.path.exists(OUTPUT_PLOT_DIR):
    os.makedirs(OUTPUT_PLOT_DIR)

# ==================== FUNCTIONS ====================
def get_fft(s, fs):
    n = len(s)
    mag = np.abs(np.fft.fft(s))[:n//2]
    freq = np.fft.fftfreq(n, 1/fs)[:n//2]
    return freq, mag

def get_baseline_energy(f, m):
    idx = np.where(f <= 0.5)
    return np.sum(m[idx]**2)

def norm(s): 
    # Tránh chia cho 0 nếu tín hiệu phẳng
    denom = s.max() - s.min()
    if denom == 0: return s
    return (s - s.min()) / denom

def prd(x, y):
    return 100 * np.linalg.norm(x - y) / np.linalg.norm(x)


def calculate_rmse(s1, s2):
    # Tính RMSE trên tín hiệu đã chuẩn hóa để đánh giá độ khớp về hình dạng
    return np.sqrt(mean_squared_error(norm(s1), norm(s2)))

# ==================== PROCESSING LOOP ====================
results = []
filt_files = glob(os.path.join(FOLDER_FILT, "*.csv"))

print(f"--- Đang xử lý {len(filt_files)} bản ghi ---")

for f_path in filt_files:
    filt_filename = os.path.basename(f_path)
    gt_filename = filt_filename.replace("_with_bw", "")
    gt_path = os.path.join(FOLDER_GT, gt_filename)
    # noise_path = os.path.join(NOSIE_GT, filt_filename)
    
    # if not os.path.exists(gt_path) or not os.path.exists(noise_path):
    #     continue

    try:
        df_filt = pd.read_csv(f_path)
        df_gt = pd.read_csv(gt_path)
        # df_noise = pd.read_csv(noise_path)

        sig_gt = df_gt["MLII"].values 
        sig_filt = df_filt["Filtered_Signal"].values 
        # sig_noise = df_noise["MLII"].values

        # min_len = min(len(sig_gt), len(sig_filt), len(sig_noise))
        min_len = min(len(sig_gt), len(sig_filt))
        s_gt = sig_gt[:min_len]
        s_filt = sig_filt[:min_len]
        # s_noise = sig_noise[:min_len]

        # --- Tính toán chỉ số ---
        corr = np.corrcoef(s_gt, s_filt)[0, 1]
        rmse_val = calculate_rmse(s_gt, s_filt)
        
        f_gt, m_gt = get_fft(s_gt, FS)
        f_filt, m_filt = get_fft(s_filt, FS)
        # f_noise, m_noise = get_fft(s_noise, FS)

        # be_raw = get_baseline_energy(f_noise, m_noise)
        be_filt = get_baseline_energy(f_filt, m_filt)
        # bw_reduction = (1 - be_filt/be_raw) * 100 if be_raw > 0 else 0
        prd_value = prd(s_gt, s_filt)
        print(f"PRD = {prd_value:.2f}%")

        results.append({
            "File": filt_filename,
            "Correlation": corr,
            "RMSE": rmse_val,
            "PRD": prd_value 
            # "BW_Reduction": bw_reduction
        })

        # --- Vẽ và lưu ảnh ---
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 10))
        
        # Subplot 1: Time Domain
        # ax1.plot(norm(s_noise), color='red', alpha=0.3, label='Noisy Input', lw=1)
        ax1.plot(norm(s_gt), color='black', alpha=0.7, label='Ground Truth', lw=1.5)
        ax1.plot(norm(s_filt), color='blue', alpha=0.8, label='Filtered (STM32)', lw=1)
        ax1.set_title(f"Record: {filt_filename}\nCorr: {corr:.4f} | RMSE: {rmse_val:.4f}")
        ax1.legend(loc='upper right')

        # Subplot 2: Frequency Domain
        # ax2.semilogy(f_noise, m_noise, color='red', alpha=0.3, label='Noisy Spectrum')
        ax2.semilogy(f_gt, m_gt, color='black', alpha=0.5, label='GT Spectrum')
        ax2.semilogy(f_filt, m_filt, color='blue', alpha=0.7, label='Filtered Spectrum')
        ax2.set_xlim(0, 60)
        # ax2.set_title(f"BW Reduction: {bw_reduction:.2f}%")
        ax2.legend(loc='upper right')

        plt.tight_layout()
        plt.savefig(os.path.join(OUTPUT_PLOT_DIR, filt_filename.replace(".csv", ".png")))
        plt.close()

    except Exception as e:
        print(f"⚠️ Lỗi file {filt_filename}: {e}")

# ==================== THỐNG KÊ TỔNG HỢP ====================
if results:
    df_res = pd.DataFrame(results)
    print("\n" + "="*60)
    print(f"{'KẾT QUẢ ĐÁNH GIÁ TỔNG HỢP':^60}")
    print("="*60)
    print(df_res[["Correlation", "RMSE",]].describe().loc[['mean', 'std', 'min', 'max']])
    
    # Vẽ phân phối các chỉ số
    plt.figure(figsize=(18, 5))
    
    # Correlation
    plt.subplot(1, 3, 1)
    sns.histplot(df_res['Correlation'], kde=True, color='green')
    plt.title("Distribution of Correlation")

    # RMSE
    plt.subplot(1, 3, 2)
    sns.histplot(df_res['RMSE'], kde=True, color='orange')
    plt.title("Distribution of RMSE")

    # # BW Reduction
    # plt.subplot(1, 3, 3)
    # sns.histplot(df_res['BW_Reduction'], kde=True, color='blue')
    # plt.title("Distribution of BW Reduction (%)")
    
    plt.tight_layout()
    plt.show()