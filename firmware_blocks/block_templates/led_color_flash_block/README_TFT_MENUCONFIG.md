# TFT Menuconfig Recovery Guide (LED Color Flash Block)

Use this guide if `sdkconfig` gets reset (for example after accidental cleanup) and you need to re-enter TFT settings.

## Project path

`firmware_blocks/block_templates/led_color_flash_block`

## 1) Open menuconfig

```powershell
idf.py menuconfig
```

## 2) LVGL base settings

Go to:

`Component config -> LVGL configuration`

Set:

- `Maximal horizontal resolution`: `240`
- `Maximal vertical resolution`: `320`
- `Color depth`: `16 (RGB565)`
- `Default display refresh period (ms)`: `20` (or `30` if you prefer)
- `DPI`: `130`

## 3) TFT display controller and pins

Go to:

`Component config -> LVGL configuration -> LVGL TFT Display controller`

Set:

- Display controller: `ILI9341`
- Display protocol: `SPI`
- SPI host: `VSPI`
- SPI transfer mode: `SIO`
- SPI duplex: `Half duplex`
- Orientation: `Portrait`

Then go to:

`Display Pin Assignments`

Set:

- `GPIO for MOSI`: `23`
- `GPIO for CLK`: `18`
- `Use CS signal`: `Enabled`
- `GPIO for CS`: `27`
- `Use DC signal`: `Enabled`
- `GPIO for DC`: `14`
- `GPIO for Reset`: `4`
- `Enable backlight control`: `Enabled`
- `Backlight ON logic level`: `HIGH (1)`
- `GPIO for Backlight Control`: `32`

## 4) Touch controller and pins (XPT2046)

Go to:

`Component config -> LVGL configuration -> LVGL Touch controller`

Set:

- Touch controller: `XPT2046`
- Touch protocol: `SPI`
- Touch SPI host: `VSPI`

Then go to:

`Touchpanel (XPT2046) Pin Assignments`

Set:

- `MISO`: `19`
- `MOSI`: `23`
- `CLK`: `18`
- `CS`: `26`
- `IRQ`: `36`

Then go to:

`Touchpanel Configuration (XPT2046)`

Set:

- `Minimum X`: `200`
- `Minimum Y`: `120`
- `Maximum X`: `1900`
- `Maximum Y`: `1900`
- `Swap XY`: `Enabled`
- `Invert X`: `Disabled`
- `Invert Y`: `Enabled`
- Detection method: `IRQ pin only`

## 5) Save and rebuild

Save and exit menuconfig, then run:

```powershell
idf.py reconfigure
idf.py build
idf.py -p COMx flash monitor
```

## 6) Quick touch sanity check

On the numpad screen:

- Tap top-left and top-right keys
- Tap bottom-left and bottom-right keys

If mapping is flipped:

- left/right wrong -> toggle `Invert X`
- top/bottom wrong -> toggle `Invert Y`
- axes crossed -> toggle `Swap XY`

## Optional (recommended)

To make recovery easier long-term, keep a `sdkconfig.defaults` committed with these TFT settings so they are reapplied automatically after fresh config generation.
