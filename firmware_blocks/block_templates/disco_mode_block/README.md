# DISCO Block

Block type: `BLOCK_TYPE_DISCO`  
Payload: `mode_id` (uint8)

Start here:
- Copy `firmware/esp32/block_templates/common_block` into this folder.
- Update `MY_BLOCK_TYPE` and `MY_ADDRESS` in `main/i2c_comm.c`.
- Add `mode_id` config and preview LED/sound tempo.

See `firmware/FRAMEWORK.md` for UX rules and contract details.
