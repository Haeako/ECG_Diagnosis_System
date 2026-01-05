import serial
import time

# Thay 'COM3' bằng cổng Arduino của bạn trên Windows, hoặc '/dev/ttyUSB0' trên Linux
ser = serial.Serial('COM16', 9600)
time.sleep(2)  # chờ Arduino reset

with open("data.csv", "w") as f:
    while True:
        try:
            line = ser.readline().decode().strip()  # đọc 1 dòng từ Serial
            print(line)                              # hiển thị trên console
            f.write(line + "\n")
            if line is None:
                break                     # ghi vào file CSV
        except KeyboardInterrupt:
            print("Stopped by user")
            break
