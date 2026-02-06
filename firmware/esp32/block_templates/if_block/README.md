# IF Block

Block type: `BLOCK_TYPE_IF`  
Payload: none

Start here:
- Copy `firmware/esp32/block_templates/common_block` into this folder.
- Update `MY_BLOCK_TYPE` and `MY_ADDRESS` in `main/i2c_comm.c`.
- Customize `command_handler.c` for IF behavior (no config payload).

See `firmware/FRAMEWORK.md` for UX rules and contract details.
