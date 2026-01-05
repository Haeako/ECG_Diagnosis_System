import serial
import pandas as pd
import time
import os
from pathlib import Path

# --- CẤU HÌNH ---
SERIAL_PORT = 'COM16'
BAUD_RATE = 115200
# INPUT_FOLDER = r"D:\DOC\Python_ws\ECG\python\dataset_real_noise\clean" 
INPUT_FOLDER = r"D:\DOC\Python_ws\ECG\python\examination_ecg\examination_ecg"
OUTPUT_FOLDER = "filtered_out_real_0_05" 
TARGET_COLUMN = "MLII"

os.makedirs(OUTPUT_FOLDER, exist_ok=True)

def send_file_to_stm32(file_path, ser):
    # Đọc dữ liệu từ file gốc
    df = pd.read_csv(file_path)
    ecg_samples = df[TARGET_COLUMN].values.astype(int).tolist()
    
    # 1. Gửi dữ liệu mẫu (Burst data)
    print(f"--- Đang xử lý file: {os.path.basename(file_path)} ---")
    burst_data = "\n".join(map(str, ecg_samples)) + "\n"
    ser.write(burst_data.encode('ascii'))
    
    # 2. Gửi chuỗi báo kết thúc truyền dữ liệu
    time.sleep(0.1) 
    ser.write(b"EOF\n") 
    print(f"Đã gửi {len(ecg_samples)} mẫu, đang chờ STM32 lọc...")

    filtered_results = [] # Danh sách chứa dữ liệu sau khi lọc

    # 3. Đợi phản hồi và thu thập dữ liệu
    while True:
        if ser.in_waiting:
            line = ser.readline().decode('ascii', errors='ignore').strip()
            
            if line == "OK":
                print(f"STM32 xác nhận hoàn tất.")
                break
            
            if line:
                try:
                    # Chuyển đổi dòng nhận được sang số float
                    val = float(line)
                    filtered_results.append(val)
                except ValueError:
                    # Nếu nhận được chuỗi không phải số (ví dụ nhiễu) thì bỏ qua
                    print(f"Bỏ qua dữ liệu lạ: {line}")

    # 4. Lưu dữ liệu đã lọc ra CSV mới
    if len(filtered_results) > 0:
        output_file_path = os.path.join(OUTPUT_FOLDER, os.path.basename(file_path))
        
        # Tạo DataFrame mới (giữ lại cột index n nếu cần)
        out_df = pd.DataFrame({
            "n": range(len(filtered_results)),
            "Filtered_Signal": filtered_results
        })
        
        out_df.to_csv(output_file_path, index=False)
        print(f"Đã lưu file đã lọc tại: {output_file_path}")
    else:
        print("Cảnh báo: Không nhận được dữ liệu lọc nào!")

def send_dataset_to_stm32_folder():
    try:
        # Thiết lập timeout để tránh treo chương trình
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2)
        print(f"Kết nối thành công cổng {SERIAL_PORT}")
        
        # Lấy danh sách file và sắp xếp
        files = sorted(Path(INPUT_FOLDER).glob("*.csv"))
        
        if not files:
            print(f"Không tìm thấy file CSV nào trong {INPUT_FOLDER}")
            return

        for file_path in files:
            # Xóa sạch bộ đệm trước khi bắt đầu file mới
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            
            send_file_to_stm32(file_path, ser)
            
            print(f"Nghỉ 1 giây trước khi sang file tiếp theo...\n")
            time.sleep(1)

    except Exception as e:
        print("Lỗi hệ thống:", e)
    finally:
        if 'ser' in locals():
            ser.close()
            print("Đã đóng kết nối Serial.")

if __name__ == "__main__":
    send_dataset_to_stm32_folder()