# NOTE Block

Block type: `BLOCK_TYPE_NOTE`  
Payload: `note_id` (uint8)

Start here:
- Copy `firmware/esp32/block_templates/common_block` into this folder.
- Update `MY_BLOCK_TYPE` and `MY_ADDRESS` in `main/i2c_comm.c`.
- Add `note_id` config and preview tone on selection.

See `firmware/FRAMEWORK.md` for UX rules and contract details.
