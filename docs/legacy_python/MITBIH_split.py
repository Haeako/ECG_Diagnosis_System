import wfdb
import pandas as pd
import json
import os
import argparse

def save_record_to_files(record, ann, output_dir):
    """Hàm bổ trợ để lưu signal và metadata"""
    record_name = record.record_name
    
    # 1. Trích xuất Channel I (Kênh đầu tiên)
    channel_index = 0
    signal_data = record.p_signal[:, channel_index]
    channel_name = record.sig_name[channel_index]
    
    # 2. Lưu CSV
    csv_path = os.path.join(output_dir, f"{record_name}.csv")
    pd.DataFrame(signal_data, columns=[channel_name]).to_csv(csv_path, index=False)
    
    # 3. Chuẩn bị Metadata
    metadata = {
        "record_name": record_name,
        "fs": record.fs,
        "sig_len": record.sig_len,
        "sig_name": record.sig_name,
        "units": record.units,
        "comments": record.comments,
        "annotations": {
            "sample": ann.sample.tolist() if ann else [],
            "symbol": ann.symbol if ann else []
        }
    }
    
    # 4. Lưu JSON
    json_path = os.path.join(output_dir, f"{record_name}.json")
    with open(json_path, 'w', encoding='utf-8') as f:
        json.dump(metadata, f, indent=4)

def process_db(data_path:str):
    print("\n--- PROCESS (ONLINE DATASET) ---")
    output_dir = "output_mitdb"
    os.makedirs(output_dir, exist_ok=True)
    
    # Danh sách đầy đủ các bản ghi của MITDB
    records = wfdb.get_record_list(data_path)
    
    for r_name in records:
        try:
            # Tải signal và annotation từ PhysioNet
            record = wfdb.rdrecord(r_name, pn_dir='mitdb')
            try:
                ann = wfdb.rdann(r_name, 'atr', pn_dir='mitdb')
            except:
                ann = None
            
            save_record_to_files(record, ann, output_dir)
            print(f"Thành công: MITDB {r_name}")
        except Exception as e:
            print(f"Lỗi bản ghi MITDB {r_name}: {e}")

def process_local(local_path):
    print("\n--- ĐANG XỬ LÝ NOISE STRESS TEST DATABASE (LOCAL) ---")
    output_dir = "output_nstdb"
    os.makedirs(output_dir, exist_ok=True)
    
    # Quét tất cả các file .hea trong thư mục để lấy tên bản ghi
    files = [f[:-4] for f in os.listdir(local_path) if f.endswith('.hea')]
    
    for r_name in files:
        try:
            # Đọc từ đường dẫn cục bộ
            record_full_path = os.path.join(local_path, r_name)
            record = wfdb.rdrecord(record_full_path)
            
            # Thử đọc annotation (nếu có)
            try:
                ann = wfdb.rdann(record_full_path, 'atr')
            except:
                ann = None
                
            save_record_to_files(record, ann, output_dir)
            print(f"Thành công: NSTDB {r_name}")
        except Exception as e:
            print(f"Lỗi bản ghi NSTDB {r_name}: {e}")

if __name__ == "__main__":
    # 1. Xử lý bộ MITDB (Tải online)
    parser = argparse.ArgumentParser(
                    prog='ProgramName',
                    description='What the program does',
                    epilog='Text at the bottom of help')
    parser.add_argument('filename')           # positional argument
    parser.add_argument('-l', '--local',
                    action='store_true')  # on/off flag
    args = parser.parse_args()
    data_path = args.filename
    if args.local is True:
        process_local(data_path)    
    process_db(data_path)
    
    # 2. Xử lý bộ Noise (Đường dẫn cục bộ của bạn)
    # path_noise = r'D:\DOC\Python_ws\ECG\data\mit-bih-noise-stress-test-database-1.0.0'
    # if os.path.exists(path_noise):
        # process_nstdb(path_noise)
    # else:
        # print(f"Đường dẫn không tồn tại: {path_noise}")