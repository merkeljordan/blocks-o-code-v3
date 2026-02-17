# LOOP Block

Block type: `BLOCK_TYPE_LOOP`  
Payload: `loop_count` (uint8)

Start here:
- Copy `firmware/esp32/block_templates/common_block` into this folder.
- Update `MY_BLOCK_TYPE` and `MY_ADDRESS` in `main/i2c_comm.c`.
- Add a `loop_count` config payload and handle `CMD_SET_LOOP` (optional).

See `firmware/FRAMEWORK.md` for UX rules and contract details.
