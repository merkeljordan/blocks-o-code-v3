# DELAY Block

Block type: `BLOCK_TYPE_DELAY`  
Payload: `delay_ms` (uint16 or uint32)

Start here:
- Copy `firmware/esp32/block_templates/common_block` into this folder.
- Update `MY_BLOCK_TYPE` and `MY_ADDRESS` in `main/i2c_comm.c`.
- Add a `delay_ms` config payload and handle `CMD_SET_DELAY` (optional).

See `firmware/FRAMEWORK.md` for UX rules and contract details.
