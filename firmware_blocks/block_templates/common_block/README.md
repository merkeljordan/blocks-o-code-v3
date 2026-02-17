# Common Block Template

This folder contains the shared baseline firmware used to start new block types.

## Included Modules
- `main.c`: init and task creation only
- `i2c_comm.c`: I2C slave + register handling
- `command_handler.c`: basic command routing
- `led_matrix.c`: WS2812 matrix driver
- `speaker.c` / `speaker.h`: LEDC PWM speaker driver

## How to Use
1. Copy this folder into a block-specific folder under `block_templates/`.
2. Update `MY_BLOCK_TYPE` and `MY_ADDRESS` in `main/i2c_comm.c`.
3. Add block-specific payload + behavior in `main/command_handler.c`.

See `firmware/FRAMEWORK.md` for contract and UX rules.
