import pandas as pd
import matplotlib.pyplot as plt
import json
import os

def visualize_all_records(data_dirs):
    """
    data_dirs: Danh sách các thư mục chứa file csv và json (ví dụ: ['output_mitdb', 'output_nstdb'])
    """
    viz_root = "visualizations"
    os.makedirs(viz_root, exist_ok=True)

    for folder in data_dirs:
        if not os.path.exists(folder):
            continue
            
        print(f"\n--- ĐANG VẼ ĐỒ THỊ TRONG THƯ MỤC: {folder} ---")
        save_path = os.path.join(viz_root, folder)
        os.makedirs(save_path, exist_ok=True)

        # Lấy danh sách các file csv
        csv_files = [f for f in os.listdir(folder) if f.endswith('.csv')]

        for file in csv_files:
            record_name = file.replace('.csv', '')
            csv_file_path = os.path.join(folder, file)
            json_file_path = os.path.join(folder, f"{record_name}.json")

            try:
                # 1. Đọc dữ liệu Signal và Metadata
                df = pd.read_csv(csv_file_path)
                with open(json_file_path, 'r') as f:
                    meta = json.load(f)

                fs = meta['fs']
                signal_name = df.columns[0]
                signal = df[signal_name].values

                # 2. Chỉ vẽ 10 giây đầu tiên để nhìn rõ waveform
                seconds_to_plot = 10
                samples_to_plot = int(seconds_to_plot * fs)
                
                # Cắt tín hiệu
                plot_signal = signal[:samples_to_plot]
                time_axis = [i / fs for i in range(len(plot_signal))]

                # 3. Vẽ đồ thị
                plt.figure(figsize=(15, 5))
                plt.plot(time_axis, plot_signal, color='red', linewidth=0.8)
                
                # Vẽ Annotations (nếu có nhãn bác sĩ trong 10s đầu)
                ann = meta.get('annotations', {})
                if ann and 'sample' in ann:
                    ann_samples = ann['sample']
                    ann_symbols = ann['symbol']
                    for s, sym in zip(ann_samples, ann_symbols):
                        if s < samples_to_plot:
                            plt.text(s/fs, plot_signal[s], sym, color='blue', fontweight='bold')
                            plt.axvline(s/fs, color='blue', alpha=0.2, linestyle='--')

                plt.title(f"Record: {record_name} | Lead: {signal_name} | Fs: {fs}Hz")
                plt.xlabel("Time (s)")
                plt.ylabel(f"Amplitude ({meta['units'][0]})")
                plt.grid(True, which='both', linestyle='--', alpha=0.5)
                
                # 4. Lưu ảnh
                img_name = os.path.join(save_path, f"{record_name}.png")
                plt.savefig(img_name, dpi=150)
                plt.close() # Đóng plot để giải phóng bộ nhớ
                
                print(f"-> Đã vẽ xong: {img_name}")

            except Exception as e:
                print(f"Lỗi khi vẽ {record_name}: {e}")

if __name__ == "__main__":
    # Danh sách các thư mục chứa dữ liệu từ bước trước
    folders_to_plot = ['output_mitdb', 'output_nstdb']
    visualize_all_records(folders_to_plot)