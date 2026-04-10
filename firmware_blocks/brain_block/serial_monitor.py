"""Serial monitor for Brain block - logs to file and stdout."""
import serial
import sys
import time

PORT = "COM3"
BAUD = 115200
LOG_FILE = "brain_serial.log"

try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
    print(f"Connected to {PORT} at {BAUD} baud. Logging to {LOG_FILE}")
    print("Press Ctrl+C to stop.")
except serial.SerialException as e:
    print(f"ERROR: Cannot open {PORT}: {e}")
    print("Make sure no other serial monitor is using this port.")
    sys.exit(1)

with open(LOG_FILE, "a", encoding="utf-8") as log:
    log.write(f"\n\n=== Session started at {time.strftime('%Y-%m-%d %H:%M:%S')} ===\n")
    try:
        while True:
            line = ser.readline()
            if line:
                text = line.decode("utf-8", errors="replace").rstrip()
                print(text)
                log.write(text + "\n")
                log.flush()
    except KeyboardInterrupt:
        print("\nMonitor stopped.")
    finally:
        ser.close()
