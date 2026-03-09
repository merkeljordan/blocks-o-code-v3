# Firmware Setup Guide

This guide will help you set up the development environment for the ESP32 Brain Block firmware.

## Prerequisites

- **Operating System**: Windows, macOS, or Linux
- **ESP-IDF**: Version 5.0 or later
- **Hardware**: ESP32 development board (e.g., ESP32-WROOM)
- **USB Cable**: For flashing and debugging

## Installing ESP-IDF

### Windows

1. Download ESP-IDF from [Espressif's official site](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/windows-setup.html)
2. Run the ESP-IDF installer
3. Follow the installation wizard
4. Open ESP-IDF Command Prompt from Start Menu

### macOS / Linux

```bash
# Install prerequisites
# macOS
brew install cmake ninja dfu-util

# Linux (Ubuntu/Debian)
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

# Clone ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.0

# Install ESP-IDF
./install.sh esp32

# Set up environment (add to ~/.bashrc or ~/.zshrc)
alias get_idf='. $HOME/esp/esp-idf/export.sh'
```

## Project Structure

```
firmware_blocks/
├── brain_block/              # Main Brain Block firmware (I2C master)
├── block_templates/          # Templates and reference implementations for child blocks
├── README.md                 # Firmware overview
└── FRAMEWORK.md              # Firmware framework documentation
```

## Building the Firmware

### 1. Navigate to Project

```bash
cd firmware_blocks/brain_block
```

### 2. Set Target

```bash
idf.py set-target esp32
```

### 3. Configure (Optional)

```bash
idf.py menuconfig
```

Key configurations:
- **Wi-Fi**: Set SSID and password
- **TCP Server IP**: IP address of your Flutter app host
- **TCP Port**: Default is `41233`
- **I2C**: GPIO pins for SDA/SCL (default: GPIO 21/22)

### 4. Build

```bash
idf.py build
```

### 5. Flash and Monitor

```bash
# Flash firmware to ESP32
idf.py flash

# Monitor serial output
idf.py monitor

# Or both at once
idf.py flash monitor
```

## Key Files

- **`main/main.c`**: Application entry point
- **`main/app.c`**: TCP client and Wi-Fi setup
- **`main/block_config_manager.c`**: Block configuration management
- **`main/i2c_comm.c`**: I2C communication
- **`main/i2c_protocol.h`**: I2C protocol definitions

## Troubleshooting

### Build Errors

- **"idf.py: command not found"**: Make sure ESP-IDF environment is activated
- **"No serial ports found"**: Check USB cable and drivers
- **Wi-Fi connection fails**: Verify credentials in `menuconfig`

### Connection Issues

- **Can't connect to companion app**: 
  - Verify TCP server IP address matches your computer's IP
  - Check firewall settings
  - Ensure companion app is running and server is started

### I2C Issues

- **No blocks detected**:
  - Check I2C wiring (SDA/SCL, power, ground)
  - Verify pull-up resistors (usually 4.7kΩ)
  - Check I2C addresses match expected values

## Next Steps

- **[Firmware Architecture](../architecture/firmware-architecture.md)** - Understand firmware design
- **[Firmware API](../api/firmware-api.md)** - API reference
- **[App Setup](./app-setup.md)** - Set up Flutter app

## Additional Resources

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [ESP32 Hardware Reference](https://www.espressif.com/en/products/socs/esp32)
- [I2C Protocol](https://en.wikipedia.org/wiki/I%C2%B2C)
