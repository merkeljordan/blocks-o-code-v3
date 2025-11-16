
# Child Block 1 - LED Matrix Display

## Overview
Child Block 1 is an I²C slave device featuring a 4x4 WS2812B LED matrix. It receives commands from the Brain Block via I²C and displays visual feedback through programmable RGB LEDs.

## Hardware
- **MCU:** ESP32 WROOM
- **Role:** I²C Slave (receives commands from Brain Block)
- **I²C Address:** 0x08
- **I²C Pins:**
  - SDA: GPIO 21
  - SCL: GPIO 22
  - Pull-ups: Internal pull-ups enabled
- **LED Matrix:**
  - Type: WS2812B (NeoPixel)
  - Configuration: 4x4 grid (16 LEDs total)
  - Data Pin: GPIO 18
  - Protocol: RMT (Remote Control) for timing-critical control

## System Architecture
```
Brain Block (I²C Master)
    │
    │ I²C Commands (0x08)
    ↓
Child Block 1 (I²C Slave)
    │
    │ RMT Signal
    ↓
WS2812B LED Matrix (16 LEDs)
```

## Code Structure (Modular)
```
child_block_1/main/
├── main.c              # ~40 lines - Entry point (init & task creation ONLY)
├── i2c_comm.c          # ~50 lines - I²C slave communication
├── led_matrix.c        # ~110 lines - WS2812B LED control
├── command_handler.c   # ~85 lines - Command processing
├── i2c_protocol.h      # ~200 lines - Protocol definitions (shared with Brain)
└── CMakeLists.txt      # Build configuration
```

### **File Breakdown**

#### **main.c** - Application Entry Point
**Responsibilities:**
- Initialize LED matrix
- Show startup animation
- Initialize I²C slave
- Create FreeRTOS tasks
- **NO business logic** - just initialization

**Key Functions:**
```c
void app_main(void)  // Called once at startup
```

---

#### **i2c_comm.c** - I²C Slave Communication
**Responsibilities:**
- Initialize I²C slave interface at address 0x08
- Receive commands from Brain Block
- Pass commands to command handler

**Key Functions:**
```c
esp_err_t i2c_slave_init(void);  // Initialize I²C slave
void i2c_task(void *arg);        // FreeRTOS task - listens for commands
```

**I²C Configuration:**
- Mode: Slave
- Address: 0x08
- RX Buffer: 128 bytes
- TX Buffer: 128 bytes

---

#### **led_matrix.c** - LED Matrix Control
**Responsibilities:**
- Initialize WS2812B LED strip via RMT
- Control individual LED colors
- Manage brightness scaling
- Provide matrix fill/clear operations

**Key Functions:**
```c
esp_err_t led_matrix_init(void);                    // Initialize LED hardware
void led_matrix_startup_animation(void);            // 3 red flashes on boot
void matrix_fill(uint8_t r, uint8_t g, uint8_t b); // Fill all LEDs
void matrix_clear(void);                            // Turn off all LEDs
void matrix_show(void);                             // Refresh display
void matrix_set_brightness(uint8_t brightness);    // Set brightness (0-255)
uint8_t matrix_get_brightness(void);               // Get current brightness
```

**LED Configuration:**
```c
GPIO Pin: 18
LED Count: 16 (4x4 matrix)
Pixel Format: GRB (WS2812B standard)
Protocol: RMT (10MHz resolution)
Default Brightness: 50 (~20%)
```

---

#### **command_handler.c** - Command Processing
**Responsibilities:**
- Parse incoming I²C commands
- Execute appropriate actions
- Provide status logging
- Track LED state

**Key Functions:**
```c
void handle_command(uint8_t *buffer, int len);  // Process I²C command
void led_status_task(void *arg);                // Status logging task
```

**Supported Commands:**
- `CMD_PING` - Heartbeat check
- `CMD_MATRIX_FILL` - Set all LEDs to RGB color
- `CMD_MATRIX_CLEAR` - Turn off all LEDs
- `CMD_MATRIX_BRIGHTNESS` - Set brightness (0-255)
- `CMD_MATRIX_SHOW` - Refresh LED display
- `CMD_RESET` - Reset to initial state

---

#### **i2c_protocol.h** - Protocol Definitions
**Responsibilities:**
- Define I²C hardware configuration
- Define command enums
- Define block addresses and types
- Shared with Brain Block and other Child Blocks

**Key Definitions:**
```c
// I²C Hardware
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22
#define I2C_FREQ_HZ         100000

// This Block
#define CHILD_1_ADDR        0x08
#define MY_BLOCK_TYPE       BLOCK_TYPE_LED

// Commands (enum)
typedef enum {
    CMD_PING            = 0x00,
    CMD_MATRIX_FILL     = 0x10,
    CMD_MATRIX_CLEAR    = 0x12,
    CMD_MATRIX_BRIGHTNESS = 0x16,
    CMD_MATRIX_SHOW     = 0x14,
    CMD_RESET           = 0xFF,
} i2c_command_t;
```

---

## Code Flow

### **Initialization Sequence**
```
1. app_main() starts
2. led_matrix_init() configures WS2812B on GPIO 18
3. led_matrix_startup_animation() shows 3 red flashes
4. i2c_slave_init() configures I²C slave at 0x08
5. xTaskCreate() starts i2c_task and led_status_task
6. Child Block 1 ready to receive commands!
```

### **Command Processing Loop**
```
1. i2c_task() waits for I²C data
2. Brain Block sends command (e.g., FILL RED)
3. i2c_slave_read_buffer() receives bytes
4. handle_command() parses command
5. Calls matrix_fill(255, 0, 0)
6. Calls matrix_show() to refresh LEDs
7. LEDs turn red!
8. Loop back to step 1
```

### **LED Update Flow**
```c
// Example: Fill matrix with blue
matrix_fill(0, 0, 255);  // Set color in buffer
matrix_show();            // Send to LEDs via RMT
```

Internally:
1. `matrix_fill()` applies brightness scaling
2. Sets all 16 LED pixel values in buffer
3. `matrix_show()` triggers RMT to send data to WS2812B
4. LEDs update in ~1ms

---

## I²C Protocol

### **Command Format**
```
[CMD_BYTE] [DATA1] [DATA2] [DATA3]
```

### **Command Reference**

| Command | Byte 0 | Byte 1 | Byte 2 | Byte 3 | Action |
|---------|--------|--------|--------|--------|--------|
| PING | 0x00 | - | - | - | Heartbeat response |
| MATRIX_FILL | 0x10 | R (0-255) | G (0-255) | B (0-255) | Fill all LEDs with RGB |
| MATRIX_CLEAR | 0x12 | - | - | - | Turn off all LEDs |
| MATRIX_BRIGHTNESS | 0x16 | Value (0-255) | - | - | Set brightness |
| MATRIX_SHOW | 0x14 | - | - | - | Refresh display |
| RESET | 0xFF | - | - | - | Reset to default state |

### **Example Commands**

**Fill Red:**
```
Brain sends: [0x10, 0xFF, 0x00, 0x00]
Result: All 16 LEDs turn red
```

**Set Brightness to 30%:**
```
Brain sends: [0x16, 0x4D]  // 0x4D = 77 = ~30%
Result: Brightness scaled to 30%
```

**Clear Matrix:**
```
Brain sends: [0x12]
Result: All LEDs turn off
```

---

## Build & Flash

### **Prerequisites**
- ESP-IDF v5.5.1 or later
- ESP32 WROOM development board
- USB cable
- WS2812B LED matrix connected to GPIO 18

### **Dependencies**
- Component: `espressif/led_strip` (managed by ESP-IDF component manager)
- Defined in: `main/idf_component.yml`

### **Build Commands**
```bash
cd firmware/esp32/child_block_1

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
I (123) CHILD_1: ========================================
I (124) CHILD_1:     CHILD BLOCK 1 - LED MATRIX
I (125) CHILD_1:     Address: 0x08
I (126) CHILD_1:     Type: LED
I (127) CHILD_1: ========================================
I (234) LED_MATRIX: Initializing LED Matrix (4x4 = 16 LEDs) on GPIO18
I (345) LED_MATRIX: LED Matrix initialized successfully!
I (456) LED_MATRIX: Startup animation...
I (567) I2C_COMM: Init I²C Slave at 0x08
I (678) I2C_COMM: I²C slave initialized successfully!
I (789) CHILD_1: Child Block 1 ready and waiting for commands!
I (890) CHILD_1: All tasks created successfully!
I (10000) CMD_HANDLER: Status: READY | LED: RGB(0,0,0) | Brightness: 19%
```

**When Brain sends commands:**
```
I (15234) I2C_COMM: Received 4 bytes
I (15235) CMD_HANDLER: Command: MATRIX_FILL (0x10), Length: 4 bytes
I (15236) CMD_HANDLER:   → FILL RGB(255, 0, 0)
I (15237) LED_MATRIX: Filling matrix RGB(255, 0, 0) @ brightness 50
```

---

## Wiring Diagram

### **I²C Connection (to Brain Block)**
```
Child Block 1          Brain Block
GPIO 21 (SDA) ←──────→ GPIO 21 (SDA)
GPIO 22 (SCL) ←──────→ GPIO 22 (SCL)
GND           ←──────→ GND
3.3V          ←──────→ 3.3V
```

### **LED Matrix Connection**
```
ESP32              WS2812B LED Matrix
GPIO 18 ──────────→ DIN (Data In)
5V      ──────────→ VCC (Power)
GND     ──────────→ GND
```

**Notes:**
- WS2812B requires 5V power
- Use external 5V supply for >8 LEDs
- Data line is 3.3V tolerant (GPIO 18)
- Add 330Ω resistor on data line (optional but recommended)
- Add 1000µF capacitor across power (recommended for stability)

---

## Current Features

### ✅ **Implemented**
- [x] I²C slave initialization at address 0x08
- [x] WS2812B LED matrix control (4x4 = 16 LEDs)
- [x] RGB color fill with brightness control
- [x] Matrix clear operation
- [x] Startup animation (3 red flashes)
- [x] Command processing with logging
- [x] Status reporting task (every 10 seconds)
- [x] Modular code structure

### ⏳ **Planned (Future)**
- [ ] Individual LED addressing (set single pixel)
- [ ] Patterns and animations
- [ ] Color cycling modes
- [ ] Brightness fade effects
- [ ] Bidirectional I²C (send status back to Brain)
- [ ] Error reporting via I²C
- [ ] Custom user patterns

---

## Integration with System

### **Brain Block Commands**
The Brain Block can send commands like:
```c
// Fill Child Block 1 with red
i2c_matrix_fill(CHILD_1_ADDR, 255, 0, 0);

// Set brightness to 30%
i2c_matrix_set_brightness(CHILD_1_ADDR, 77);

// Clear the matrix
i2c_matrix_clear(CHILD_1_ADDR);
```

### **Demo Sequence**
In the Brain Block's demo task, Child Block 1 cycles through:
1. Set brightness to 30%
2. Fill RED (1 second)
3. Fill GREEN (1 second)
4. Fill BLUE (1 second)
5. CLEAR (1 second)
6. Repeat

---

## Troubleshooting

### **LEDs Don't Light Up**

**Symptoms:** No LEDs turn on, even during startup animation

**Possible Causes:**
1. LED matrix not powered
2. Wrong GPIO pin
3. Incorrect wiring
4. Faulty LED strip

**Solutions:**
- Verify 5V power to LED strip
- Check GPIO 18 connection to DIN pin
- Test with known-good LED strip
- Check serial output for initialization errors

---

### **LEDs Flicker or Show Wrong Colors**

**Symptoms:** LEDs flicker, show random colors, or unstable

**Possible Causes:**
1. Insufficient power supply
2. Missing ground connection
3. Data line too long or noisy
4. Capacitor missing

**Solutions:**
- Use external 5V power supply (not USB)
- Verify GND connection between ESP32 and LED strip
- Add 330Ω resistor on data line
- Add 1000µF capacitor across LED power
- Keep data wire short (<1 meter)

---

### **I²C Communication Fails**

**Symptoms:** Brain Block can't detect Child Block 1

**Possible Causes:**
1. Wrong I²C address
2. SDA/SCL wires swapped
3. Missing pull-up resistors
4. I²C bus conflict

**Solutions:**
- Verify address is 0x08 in both Brain and Child code
- Check I²C wiring (SDA=21, SCL=22)
- Add 4.7kΩ pull-up resistors if needed
- Ensure only one device uses address 0x08

---

### **Startup Animation Works, Commands Don't**

**Symptoms:** 3 red flashes appear on boot, but no response to commands

**Possible Causes:**
1. I²C slave not initialized
2. I²C task crashed
3. Command handler error

**Solutions:**
- Check serial output for I²C init errors
- Look for task crash messages
- Verify Brain Block is sending to correct address (0x08)

---

### **Build Errors**

**Error:** `led_strip.h: No such file or directory`

**Solution:** Make sure `idf_component.yml` exists and run:
```bash
idf.py reconfigure
idf.py build
```

**Error:** `Undefined reference to 'led_strip_new_rmt_device'`

**Solution:** Verify `CMakeLists.txt` has `led_strip` in REQUIRES:
```cmake
REQUIRES driver led_strip
```

---

## Development Guidelines

### **Adding New LED Patterns**

1. Create function in `led_matrix.c`:
```c
void matrix_rainbow(void) {
    // Your pattern code
}
```

2. Add command to `i2c_protocol.h`:
```c
CMD_MATRIX_RAINBOW = 0x20,
```

3. Handle in `command_handler.c`:
```c
case CMD_MATRIX_RAINBOW:
    matrix_rainbow();
    matrix_show();
    break;
```

### **Code Style**
- Use ESP-IDF logging: `ESP_LOGI()`, `ESP_LOGE()`
- Check return values with `ESP_ERROR_CHECK()`
- Add comments for complex LED operations
- Keep functions focused (single responsibility)

---

## Testing

### **Manual Testing**
1. Flash Child Block 1
2. Power on (should see 3 red flashes)
3. Flash Brain Block
4. Power on Brain Block
5. Observe LED matrix cycling through colors
6. Verify serial output shows command reception

### **Expected Behavior**
- **Boot:** 3 red flashes (startup animation)
- **Demo:** LEDs cycle RED → GREEN → BLUE → CLEAR every ~4 seconds
- **Serial:** Shows received commands and status updates

### **Unit Testing (Future)**
- Test individual LED addressing
- Test brightness scaling accuracy
- Test I²C command parsing
- Test error handling

---

## Performance Characteristics

| Metric | Value |
|--------|-------|
| LED Update Rate | ~1ms per frame |
| I²C Response Time | <10ms |
| Command Processing | <5ms |
| Brightness Levels | 256 (0-255) |
| Color Depth | 24-bit RGB (16.7M colors) |
| LED Refresh Rate | On-demand (not continuous) |

---

## Power Consumption

| State | Current Draw |
|-------|--------------|
| Idle (LEDs off) | ~80mA |
| All LEDs white (full) | ~960mA (16 LEDs × 60mA) |
| All LEDs @ 30% brightness | ~300mA |
| Typical usage | ~200-400mA |

**Note:** Power draw depends heavily on color and brightness. Use adequate power supply!

---

## Resources

### **Documentation**
- [ESP-IDF I²C Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2c.html)
- [ESP32 RMT (Remote Control)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/rmt.html)
- [WS2812B Datasheet](https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf)
- [LED Strip Component](https://components.espressif.com/components/espressif/led_strip)

### **Related Projects**
- Brain Block: `../brain_block/README.md`
- Child Block 2: `../child_block_2/README.md`
- Protocol Definitions: `main/i2c_protocol.h`

---

## Contributors
- Destiny - Child Block 1 development & LED matrix control
- Jordan - Future: Flutter app integration

---

## Version History

### **v1.0 - Modular Refactor (Current)**
- Split monolithic main.c into modules
- Created led_matrix.c for WS2812B control
- Created i2c_comm.c for I²C slave logic
- Created command_handler.c for command processing
- Improved code maintainability and readability

### **v0.1 - Initial Implementation**
- Basic I²C slave functionality
- WS2812B LED matrix control
- Color fill and clear operations
- All code in single main.c file

---

## License
Educational project - Blocks o' Code v3
