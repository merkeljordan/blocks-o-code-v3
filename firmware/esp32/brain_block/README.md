# Brain Block - I²C Master Controller

## Overview
The Brain Block is the central coordinator of the Blocks o' Code system. It acts as an I²C master that discovers, manages, and sends commands to multiple child blocks connected via the I²C bus.

## Hardware
- **MCU:** ESP32-WROOM
- **Role:** I²C Master (coordinates all child blocks)
- **I²C Pins:**
  - SDA: GPIO 21
  - SCL: GPIO 22
  - Pull-ups: Internal pull-ups enabled
  - Clock Speed: 100 kHz (Standard Mode)

## System Architecture
```
Brain Block (I²C Master @ GPIO 21/22)
    │
    │ I²C Bus Commands
    ├─────────────────┬─────────────────┐
    │                 │                 │
    ↓                 ↓                 ↓
Child Block 1    Child Block 2    Child Block 3
LED Matrix       OLED Display     (Future)
(Addr 0x08)      (Addr 0x09)      (Addr 0x0A)
```

## Code Structure (Modular)
```
brain_block/main/
├── main.c              # ~18 lines - Entry point (init & task creation ONLY)
├── i2c_comm.c          # ~140 lines - I²C master communication
├── demo_task.c         # ~85 lines - Demo sequence (color cycling)
├── i2c_protocol.h      # ~200 lines - Protocol definitions & enums
└── CMakeLists.txt      # Build configuration
```

### **File Breakdown**

#### **main.c** - Application Entry Point
**Responsibilities:**
- Initialize I²C master
- Perform initial device scan
- Create demo task
- **NO business logic** - just initialization

**Key Functions:**
```c
void app_main(void)  // Called once at startup
```

---

#### **i2c_comm.c** - I²C Master Communication
**Responsibilities:**
- Initialize I²C master interface
- Scan I²C bus for connected child blocks
- Send commands to child blocks (ping, fill, clear, brightness)
- Low-level I²C protocol handling

**Key Functions:**
```c
esp_err_t i2c_master_init(void);
esp_err_t i2c_ping(uint8_t addr);
void i2c_safe_scan(void);
esp_err_t i2c_matrix_fill(uint8_t address, uint8_t r, uint8_t g, uint8_t b);
esp_err_t i2c_matrix_clear(uint8_t address);
esp_err_t i2c_matrix_set_brightness(uint8_t address, uint8_t brightness);
esp_err_t i2c_oled_text(uint8_t address, const char *msg);
```

---

#### **demo_task.c** - Demo Sequence Task
**Responsibilities:**
- Continuously run demo sequence
- Cycle through colors on LED matrix
- Display color info on OLED
- Demonstrate multi-device coordination

**Key Functions:**
```c
void demo_task(void *arg);  // FreeRTOS task - runs in loop
```

**Demo Sequence:**
1. Scan I²C bus for devices
2. If Child Block 1 (LED Matrix) found:
   - Set brightness to 30%
   - Cycle: RED → GREEN → BLUE → CLEAR
3. If Child Block 2 (OLED Display) found:
   - Set brightness
   - Cycle: YELLOW → CYAN → MAGENTA → CLEAR
4. Wait 2 seconds and repeat

---

#### **i2c_protocol.h** - Protocol Definitions
**Responsibilities:**
- Define I²C hardware configuration
- Define child block addresses
- Define command enums
- Define block types
- Utility functions for debugging

**Key Definitions:**
```c
// Hardware Config
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22
#define I2C_FREQ_HZ         100000

// Addresses
#define CHILD_1_ADDR        0x08  // LED Matrix
#define CHILD_2_ADDR        0x09  // OLED Display

// Commands (enum)
typedef enum {
    CMD_PING            = 0x00,
    CMD_MATRIX_FILL     = 0x10,
    CMD_MATRIX_CLEAR    = 0x12,
    CMD_MATRIX_BRIGHTNESS = 0x16,
    // ... (see file for full list)
} i2c_command_t;

// Block Types (enum)
typedef enum {
    BLOCK_TYPE_BRAIN    = 0x00,
    BLOCK_TYPE_LED      = 0x20,
    // ... (see file for full list)
} block_type_t;
```

---

## Code Flow

### **Initialization Sequence**
```
1. app_main() starts
2. i2c_master_init() initializes I²C master on GPIO 21/22
3. i2c_safe_scan() scans bus (0x08-0x0F) for devices
4. xTaskCreate() starts demo_task in background
5. Brain Block ready!
```

### **Demo Task Loop**
```
1. Scan I²C bus for devices
2. For each detected device:
   a. Ping device (check if still alive)
   b. Send brightness command
   c. Cycle through colors (fill commands)
   d. Send clear command
3. Wait 2 seconds
4. Repeat
```

### **I²C Communication Example**
```c
// Send "Fill Red" command to Child Block 1
i2c_matrix_fill(CHILD_1_ADDR, 255, 0, 0);

// Internally this:
// 1. Creates I²C command handle
// 2. Sends START condition
// 3. Addresses device 0x08
// 4. Sends [CMD_MATRIX_FILL, 255, 0, 0] (4 bytes)
// 5. Sends STOP condition
// 6. Waits for ACK (100ms timeout)
```

---

## I²C Protocol

### **Command Format**
All commands follow this structure:
```
[CMD_BYTE] [DATA1] [DATA2] [DATA3] ...
```

### **Supported Commands**

| Command | Byte 0 | Byte 1 | Byte 2 | Byte 3 | Description |
|---------|--------|--------|--------|--------|-------------|
| PING | 0x00 | - | - | - | Check if device alive |
| MATRIX_FILL | 0x10 | R | G | B | Fill all LEDs with RGB color |
| MATRIX_CLEAR | 0x12 | - | - | - | Turn off all LEDs |
| MATRIX_BRIGHTNESS | 0x16 | Value | - | - | Set brightness (0-255) |
| OLED_TEXT | 0xF1 | Len | char[] | - | Display text on OLED |

### **I²C Address Map**
- `0x08` - Child Block 1 (LED Matrix)
- `0x09` - Child Block 2 (OLED Display)
- `0x0A-0x15` - Reserved for future child blocks

---

## Build & Flash

### **Prerequisites**
- ESP-IDF v5.5.1 or later
- ESP32 development board
- USB cable

### **Build Commands**
```bash
cd firmware/esp32/brain_block

# Build project
idf.py build

# Flash to ESP32
idf.py -p COM3 flash

# Monitor serial output
idf.py -p COM3 monitor

# Flash + Monitor (combined)
idf.py -p COM3 flash monitor
```

### **Expected Serial Output**
```
I (123) BRAIN: === BRAIN BLOCK ===
I (124) I2C_COMM: Init I²C Master: SDA=21, SCL=22
I (234) I2C_COMM: === SAFE I²C SCAN ===
I (235) I2C_COMM: Detected device at 0x08
I (236) I2C_COMM: Detected device at 0x09
I (237) I2C_COMM: Devices detected: 2
I (238) BRAIN: Brain Block initialized!

I (2240) DEMO: --- NEW CYCLE ---
I (2241) I2C_COMM: === SAFE I²C SCAN ===
I (2242) I2C_COMM: Detected device at 0x08
I (2243) I2C_COMM: Detected device at 0x09
I (2244) I2C_COMM: Devices detected: 2
I (2245) DEMO: Child 1 detected!
I (2246) DEMO: Child 1: Setting brightness to 30%
I (2750) DEMO: Child 1: RED
I (3750) DEMO: Child 1: GREEN
...
```

---

## Current Features

###  **Implemented**
- [x] I²C master initialization
- [x] Device scanning and discovery (0x08-0x0F)
- [x] Basic command transmission
  - [x] PING
  - [x] MATRIX_FILL
  - [x] MATRIX_CLEAR
  - [x] MATRIX_BRIGHTNESS
  - [x] OLED_TEXT
- [x] Demo sequence (color cycling)
- [x] Multi-device coordination
- [x] Modular code structure

### ⏳ **Planned (Future)**
- [ ] WiFi connectivity
- [ ] TCP server for Flutter app
- [ ] Device manager (track device state)
- [ ] Command parser (parse Flutter commands)
- [ ] Bidirectional I²C (read data from child blocks)
- [ ] Device capability discovery
- [ ] Error handling and retry logic
- [ ] Over-the-air (OTA) updates

---

## Integration with Child Blocks

### **Child Block 1 - LED Matrix (0x08)**
**Responds to:**
- `CMD_MATRIX_FILL` → Lights up all 16 LEDs with specified RGB color
- `CMD_MATRIX_CLEAR` → Turns off all LEDs
- `CMD_MATRIX_BRIGHTNESS` → Adjusts LED brightness (0-255)

### **Child Block 2 - OLED Display (0x09)**
**Responds to:**
- `CMD_MATRIX_FILL` → Displays color name and RGB values on screen
- `CMD_MATRIX_CLEAR` → Shows "CLEAR" state
- `CMD_MATRIX_BRIGHTNESS` → Displays brightness percentage

**Same commands, different outputs!** Demonstrates modularity.

---

## Development Guidelines

### **Adding New Commands**
1. Add command enum to `i2c_protocol.h`
2. Add case to `command_to_string()` helper
3. Implement command function in `i2c_comm.c`
4. Use in `demo_task.c` or future command parser

### **Adding New Child Blocks**
1. Add address definition to `i2c_protocol.h`
2. Update `I2C_CHILD_MAX_ADDR` if needed
3. Child block implements I²C slave at new address
4. Brain auto-discovers during scan

### **Code Style**
- Use ESP-IDF logging: `ESP_LOGI()`, `ESP_LOGE()`, etc.
- Check return values: `ESP_ERROR_CHECK()` for critical calls
- Add comments for complex I²C sequences
- Keep functions focused (single responsibility)

---

## Testing

### **Manual Testing**
1. Flash Brain Block
2. Flash Child Block 1 and/or Child Block 2
3. Power on all blocks
4. Observe serial output
5. Verify LED colors match serial log
6. Verify OLED displays correct info

### **Expected Behavior**
- Brain scans and finds connected child blocks
- Child Block 1 cycles through RED → GREEN → BLUE → CLEAR
- Child Block 2 cycles through YELLOW → CYAN → MAGENTA → CLEAR
- Each color displays for 1 second
- Cycle repeats every ~8 seconds

---

## Resources

### **Documentation**
- [ESP-IDF I²C Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2c.html)
- [ESP32 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- [I²C Protocol Specification](https://www.nxp.com/docs/en/user-guide/UM10204.pdf)

### **Related Projects**
- Child Block 1: `../child_block_1/README.md`
- Child Block 2: `../child_block_2/README.md`
- Protocol Definitions: `main/i2c_protocol.h`

---

## Contributors
- Destiny - Brain Block development & I²C protocol
- Jordan - Future: Flutter app integration, TCP server

---

## Version History

### **v1.0 - Modular Refactor (Current)**
- Split monolithic main.c into modules
- Created i2c_comm.c for I²C logic
- Created demo_task.c for demo sequence
- Improved code maintainability

### **v0.1 - Initial Implementation**
- Basic I²C master functionality
- Device scanning
- Color cycling demo
- All code in single main.c file

---

## License
Educational project - Blocks o' Code v3
