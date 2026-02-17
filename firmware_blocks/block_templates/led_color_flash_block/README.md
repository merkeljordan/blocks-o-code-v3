# LED_FLASH Block

Block type: `BLOCK_TYPE_LED_FLASH`  
Payload: `color_id` (uint8)

Start here:
- Copy `firmware/esp32/block_templates/common_block` into this folder.
- Update `MY_BLOCK_TYPE` and `MY_ADDRESS` in `main/i2c_comm.c`.
- Add `color_id` config and preview color on matrix + addressable LEDs.

See `firmware/FRAMEWORK.md` for UX rules and contract details.
