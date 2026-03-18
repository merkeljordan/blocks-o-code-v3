## GPIO pinouts (source of truth)

This document is intended to be the **single source of truth** for how ESP32 GPIO pins are used across the Blocks o' Code v3 hardware (Brain + child blocks).

- **Audience**: firmware engineers, PCB / hardware designers, and app developers who need to know which pins are safe to repurpose.
- **Scope**: only documents *actual, wired* connections on the current board revisions. Keep this file in sync with PCB design files under `pcb_files/` as they are updated.

---

## System‑wide signals

All GPIO signals on the shared PCB are **system‑wide**: the same ESP32 pins and functions apply to the Brain and every child block revision that uses this board. None of these may be repurposed without updating both firmware and PCB designs.

| GPIO | Signal name | Function | Used by | Notes |
|------|-------------|----------|--------|-------|
| 21   | SDA         | I2C data line | Brain (master), all child blocks | 100 kHz Standard Mode I2C. See `docs/hardware/block-inventory.md`. |
| 22   | SCL         | I2C clock line | Brain (master), all child blocks | 100 kHz Standard Mode I2C. See `docs/hardware/block-inventory.md`. |

When in doubt, **assume GPIO 21/22 are reserved exclusively for I2C**.

---

## Brain + child block pinout (shared PCB)

The Brain and all child blocks share **the same PCB and ESP32 pinout**. This section documents how those shared pins are wired to the TFT, touch, speaker, LEDs, and any other on‑board peripherals.

Pin assignments below are taken from the PCB schematic (see the labeled net names like `LED_D_IN`, `LED_D_IN_MAT`, `TFT_DC`, etc.).

| GPIO | Signal name | Function / peripheral | Notes |
|------|-------------|-----------------------|-------|
| 21   | SDA         | I2C data (system bus) | Shared with all blocks; see table above. |
| 22   | SCL         | I2C clock (system bus) | Shared with all blocks; see table above. |
| 14   | TFT_DC      | TFT display data/command | Net label: `TFT_DC`. |
| 4    | TFT_RST     | TFT display reset | Not shown in the provided schematic snippet. |
| 27   | TFT_CS      | TFT display chip select | Net label: `TFT_CS`. |
| 32   | TFT_LED     | TFT backlight control | Net label: `TFT_LED`. |
| 26   | T_CS        | Touch controller chip select | Net label: `T_CS`. (Touch controller likely shares SPI with TFT.) |
| TBD  | TOUCH_IRQ   | Touch controller interrupt | Not shown in the provided schematic snippet. |
| 18   | MCU_SCLK    | SPI clock | Net label: `MCU_SCLK`. |
| 19   | MCU_MISO    | SPI MISO | Net label: `MCU_MISO`. |
| 23   | MCU_MOSI    | SPI MOSI | Net label: `MCU_MOSI`. |
| 13   | LED_STRIP    | Addressable LED strip data | Net label: `LED_D_IN`. |
| 15   | LED_MATRIX| LED matrix data in | Net label: `LED_D_IN_MAT`. |
| 16   | LED_ERROR   | Status LED (error) | Net label: `LED_ERROR`. |
| 17   | LED_CORRECT | Status LED (correct) | Net label: `LED_CORRECT`. |
| 33   | LED_STATUS  | Status LED (general) | Net label: `LED_STATUS`. |
| 25   | ESP32_DAC   | Speaker / audio output (DAC) | Net label: `ESP32_DAC`. |

> **How to update:** When the Brain PCB changes, update this table first, then cross‑check any hard‑coded pin numbers in Brain firmware (e.g. `firmware_blocks/brain_block/`).

---

## Per‑block additions (if any)

Most blocks use the **shared PCB pinout** above. If a future block type requires extra pins (e.g. additional buttons, sensors), add a **sub‑section per block type** with its specific extra pins here (and keep them in sync with the schematic).

---

## Per‑block overrides (if any)

Use this section only when a particular block does **not** match the shared template above (for example, a prototype rev or a special‑purpose block).

- **Block name**: `<BLOCK_TYPE_…>`  
  - **PCB rev**: `<REV>`  
  - **Notes**: Describe why this block deviates and list its pin mapping here.

---

## Keeping firmware and PCB in sync

- **Source of truth**: This markdown file and the PCB schematics/layout under `pcb_files/` must agree.
- **Change policy**:
  - When a pin changes on the schematic, **update this file in the same PR**.
  - When firmware needs a new peripheral on a given pin, coordinate with hardware to confirm availability before merging.
- **Consumers**:
  - Firmware: ESP32 pin constants, board init code, and driver configs.
  - Hardware: PCB layout, silk labels, and connector pin numbering.

If you are unsure about a pin assignment, prefer marking it as **TBD** here and following up with the hardware owner, rather than guessing a GPIO number.

