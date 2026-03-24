# Final Todo — Blocks o' Code v3

Split between **Jordan** and **Destiny**. Branch status and assignment notes below.

**For implementers:** Each task has a **How** section with step-by-step pointers—which files to open, what to change, and how it fits together.

---

## Branch status (vs `main`)

| Branch | Status vs main | Notes |
|--------|----------------|--------|
| `main` | baseline | Local `main` matches `origin/main`. |
| `origin/led-strip-behavior` | ahead of `main` | Remote branch still exists and has unmerged work (**8 commits ahead of `main`**). |
| `origin/control-flow-docs-and-blocks` | ahead of `main` | Remote branch still exists and has unmerged work (**11 commits ahead of `main`**). |
| `note-block-firmware` (local) | ahead of `main` | Local branch has NOTE block work (**7 commits ahead of `main`**); `origin/note-block-firmware` is deleted. |
| `origin/feature/battery-monitor` | mostly merged | Battery % TFT work is merged to `main` (branch has no unique commits vs `main`). |
| Legacy refs (`origin/brain-stop-button-tft*`, `origin/music-sequences-behavior`, `origin/note-block-firmware`) | deleted on remote | Track final status from `main` plus surviving branches above. |

---

## Todo list with assignments

### 1. Stop button on Brain (Start → Stop when running)

- **What:** Once Start is pressed, the same button becomes Stop until execution finishes.
- **Where:** Brain TFT UI — `firmware_blocks/brain_block/main/esp32_lcd_display_v9.c` (Start button, `home_action_event_cb`).
- **Branch:** Merged to `main` (legacy stop-button branch no longer exists on `origin`).
- **Owner:** **Destiny** (TFT / “what you see”).
- **Status:** [x] Done (merged to `main`)
- **How:**
  1. **One physical button, two behaviors:** The Start button is created in `create_home_screen()` — search for `"Start"` and `create_action_tile(..., "Start", ...)`. Change this single button's label and action depending on whether the Brain is *idle* or *running*.
  2. **Know when we're running:** In `brain_event_handler.h`, `brain_executor_get_context()` returns a struct whose `.state` is `EXECUTOR_RUNNING` when a run is in progress (or `EXECUTOR_IDLE`, `EXECUTOR_DONE`, etc.). Include that header in the display file and call it when handling the button and when updating the button's look.
  3. **When idle:** Button shows "Start" (and maybe `LV_SYMBOL_PLAY`). On press → `brain_event_handle_message("START")` (already there).
  4. **When running:** Button shows "Stop" (and maybe `LV_SYMBOL_STOP`). On press → `brain_event_handle_message("STOP")`. The handler already supports `"STOP"` and calls `brain_executor_stop()` in `brain_event_handler.c`.
  5. **Updating the button:** When execution starts/ends, the button must switch between Start and Stop. Easiest: from the LVGL task (or a small timer), periodically read `brain_executor_get_context()->state`. If `EXECUTOR_RUNNING`, set the button label to "Stop" and make the callback send `"STOP"`; otherwise "Start" and send `"START"`.
  6. Update `s_status_label` too (e.g. "Running… tap Stop to cancel" when running).

---

### 2. Implement note block firmware

- **What:** Complete the NOTE block end-to-end: kid selects A–G on TFT (preview), submits selection to Brain, then on NOTE steps the Brain broadcasts the selected note so every speaker-capable block plays it.
- **Where:** `firmware_blocks/block_templates/note_block/` — `i2c_comm.c` and command handler in `main.c`; `i2c_protocol.h` has `CMD_PLAY_NOTE`.
- **Branch:** `note-block-firmware` local branch (origin branch deleted; needs merge/cherry-pick to `main`)
- **Owner:** **Jordan**
- **Status:** [x] Implemented on local branch (needs merge/cherry-pick to `main`)
- **How:**
  1. **TFT UI (NOTE block):** `note_block/main/tft_ui.c` provides an LVGL UI like LED Color Flash: tap **A–G** to preview, tap **SUBMIT** to publish the selection to the Brain.
  2. **Selection submit (child → Brain):** NOTE block sets `STATUS_DATA_READY` only when a selection submit is pending; Brain reads `CMD_GET_DATA` payload `[event_id=0x01, note_id]`.
  3. **Broadcast on NOTE steps (Brain):** when executor hits a NOTE step, Brain broadcasts `CMD_PLAY_NOTE <note_id>` so **every speaker-capable block** plays it:
     - NOTE blocks
     - Music sequence blocks
     - LED Color Flash block (speaker present)
     - Brain speaker (plays locally too)
  4. **Frequency mapping:** note_id 0–6 maps to A–G with frequencies:
     - A: 220Hz
     - B: 246.94Hz
     - C: 261.63Hz
     - D: 293.66Hz
     - E: 329.63Hz
     - F: 349.32Hz
     - G: 392Hz
  5. **PC UI preview:** `pc_sim/note_block/` + `docs/LVGL-simulator.md` allow running the NOTE TFT UI in a desktop window (SDL/LVGL) for quick iteration.
  6. **Deferred:** Standardizing the NOTE block I2C address will be handled on a separate branch.

---

### 3. Fix up protocol docs + change startup sound

- **What:** (a) Align protocol documentation (e.g. `docs/api/firmware-api.md`, `firmware_blocks/include/i2c_protocol.h`, any protocol-specific doc) with code. (b) Change/replace startup (boot) sound (e.g. `bootupsound.wav` / `speaker_play_boot_sound()` in shared audio). Find where components can be shared across all blocks.
- **Where:** Docs under `docs/`; `firmware_blocks/shared_components/audio/` and block-level `speaker.cpp` / WAV assets.
- **Branch:** Shared audio + shared LED UX components have landed on `main`. Remaining work can be done on a small `docs-update` / `startup-sound` branch.
- **Owner:** Jordan
- **Status:** [ ] Done 
- **How:**
  1. **Protocol docs (Jordan):** Open `docs/api/firmware-api.md` and `firmware_blocks/include/i2c_protocol.h`. Cross-check: every command in the header (e.g. `CMD_PING`, `CMD_SET_LED`, `CMD_PLAY_NOTE`, `CMD_EXECUTE`, …) is described in the doc; register map (REG_WHOAMI, REG_STATUS, …) matches. Update the doc if you add or rename commands. If there’s a separate “protocol” doc in `docs/`, align it with the same source of truth (`i2c_protocol.h`).
  2. **Startup sound:** The boot (startup) sound is played by `speaker_play_boot_sound()` from shared or block-level audio. Find it: grep for `speaker_play_boot_sound` — it’s in `firmware_blocks/shared_components/audio/speaker.cpp` and in each block’s `speaker.cpp`. The actual sound is usually a WAV file (e.g. `bootupsound.wav`) embedded via the build. To *change* the sound: replace that WAV file (same name or update the embed path in CMakeLists.txt / component config) and rebuild. To make it shared: ensure all blocks that need it use the shared component (see `protocol-docs-consistency` branch for how shared audio/LED is set up) so one WAV change affects all blocks.

---

### 4. Write out control flow blocks

- **What:** Document and/or implement execution behavior for If/Then/End If, Loop/End Loop, Delay (and any related blocks). “Write out” = docs and/or firmware execution path.
- **Where:** Docs (e.g. `docs/architecture/`, `docs/api/firmware-api.md`); brain executor / event handler; child block templates.
- **Branch:** `origin/control-flow-docs-and-blocks` (active; ahead of `main`)
- **Owner:** **Jordan** (validation / execution flow).
- **Status:** [x] Implemented on branch (needs merge to `main`; handoff to Destiny complete)
- **How:**
  1. **Execution path:** The Brain runs the program in `brain_event_handler.c`: the executor has a program (array of block types), a program counter (`pc`), and loop stack. It “ticks” and for each step may send I2C commands (e.g. CMD_EXECUTE) to the block at the current position. Control flow means: when the executor hits an **If**, it must decide whether to run the “then” branch (e.g. check button state); for **Loop**, it must repeat a range of program indices; for **Delay**, it must wait then advance. Read `brain_executor_tick()` and the switch on block type to see where to add or extend logic for IF/THEN/END_IF, LOOP/END_LOOP, DELAY.
  2. **Event map:** `block_config_manager` (and the event map) tells the Brain how many IF/LOOP boundaries there are and where they are. The executor uses this to know “loop from pc X to Y” or “if branch from A to B”. Docs: describe this in `docs/architecture/` or `docs/api/firmware-api.md` (e.g. “Control flow execution” subsection: how IF/LOOP/DELAY are interpreted and how the executor advances `pc`).
  3. **Child blocks:** Control flow *blocks* (if_block, loop_block, etc.) have minimal firmware today — mostly stubs. Their `command_handle()` may just acknowledge CMD_EXECUTE without doing much. “Write out” can mean: document what the Brain sends to them (e.g. CMD_SET_LOOP with count, CMD_EXECUTE as “you’re the current step”), and implement any config (e.g. loop count) in those templates so the Brain’s view matches.

---

### 5. Finish music sequence block + add more songs

- **What:** Complete music sequence block behavior and add more songs/assets.
- **Where:** Music sequence block template and app-side config; audio/song assets.
- **Branch:** Merged to `main` (legacy `origin/music-sequences-behavior` branch has been removed).
- **Owner:** **Destiny** (speaker / “what you hear”).
- **Status:** [x] Implemented + pushed (merged to `main`)
- **How:**
  1. **Where the block lives:** `firmware_blocks/block_templates/music_sequence_block/`. Main logic: `main/main.c` has `command_handle()` which on `CMD_EXECUTE` calls `speaker_play_song()`. The TFT UI is in `main/tft_ui.c` (song selection, etc.). Audio/songs are in the block's `components/audio/` or similar — look for WAV files or song IDs.
  2. **Finish behavior:** Ensure that when the Brain sends CMD_EXECUTE (and any config command for "which song"), the block plays the selected song from start to finish and reports ready again. Match the pattern of the LED Color Flash block: config → submit selection → Brain later sends CMD_EXECUTE to all blocks; music block should play one full song per CMD_EXECUTE.
  3. **Add more songs:** Add new WAV or song data files (or new entries in a song table) in the music sequence block's assets. Update the UI (e.g. list of songs in `tft_ui.c`) and any song-ID mapping so the user can pick the new song and the Brain can request it. If songs are stored as embedded WAVs, add the new file and register it in the build (CMakeLists.txt or component config).

---

### 6. LED strip: idle type–color mapping + execution mirroring

- **What:** Implement idle behavior (block type → color mapping) and execution mirroring for LED strip, reusing existing `led_strip` driver patterns.
- **Where:** Brain and/or child block firmware using LED strip; `led_strip` driver / shared LED UX.
- **Branch:** `origin/led-strip-behavior` has execution mirroring work pushed (ahead of `main`) — merge, then finish idle mapping + polish.
- **Owner:** **Destiny** (LEDs / peripherals).
- **Status:** [x] In progress + needs to be applied to updated control flow block logic ( needs merge to `main`)
- **What was completed:**
  1. Added idle type-color mapping across the system:
     - `BRAIN` = red
     - `IF` / `THEN` / `END_IF` = green
     - `LOOP` / `END_LOOP` = blue
     - `DELAY` = orange
     - `BUTTON` = magenta
     - `NOTE` = yellow
     - `MUSIC_SEQ` = cyan/teal
     - `LED_FLASH` = purple
  2. Brain now renders a local LED strip “program map”:
     - before scan: solid red fallback
     - after scan: one strip segment per block in the built program
     - while executing: current block segment is highlighted
  3. Child blocks were updated to accept shared `CMD_MATRIX_*` strip commands so Brain-driven mirroring works consistently.
  4. `led_color_flash_block` was polished further:
     - idle/data-ready = solid type color
     - busy/executing = status strip mirrors the live local matrix animation output
     - error = red
  5. Added inline comment blocks in Brain and child firmware to explain the execution-mirroring flow.
  6. Fixed shared `status_strip` compatibility so it works with both `led_strip` API variants used in this repo.
- **How I did it:**
  1. Brain-side mirroring is driven from executor state and `pc`, mapping the active step to a highlighted segment on the Brain strip and child strips.
  2. Child strips render their idle color through the shared `status_strip` component.
  3. During execution, `led_color_flash_block` switches from solid status color to local-output mirroring while `STATUS_BUSY` is active.
  4. Shared strip rendering uses the existing fill / brightness / show flow rather than a new LED pipeline.
- **How:**
  1. **Existing pieces:** Shared LED UX is in `firmware_blocks/shared_components/led_ux/led_ux.c` — `led_ux_show_startup()`, `led_ux_show_running()`, `led_ux_show_ok()`, `led_ux_show_error()`. These use `led_matrix.h` (e.g. `matrix_fill`, `matrix_show`, `matrix_clear`). The **LED Color Flash** block has a full `led_matrix.c` and `command_handler.c` that drive the strip with patterns. Reuse those patterns: same driver, same “fill/show/clear” style.
  2. **Idle type–color mapping:** When no block is “active”, each block (or the Brain’s strip, if it’s one strip for the whole chain) should show a *color per block type* (e.g. If = blue, Loop = green, Note = yellow). You need a small table: `block_type_t` → RGB or color_id. On idle, either the Brain tells each block “show your type color” or each block’s firmware sets its segment to that color. Where the strip is (Brain vs per-block) decides where this logic lives.
  3. **Execution mirroring:** When the executor is running and “current block” is at index N, the strip should *mirror* that (e.g. highlight the Nth block’s LEDs or animate that segment). So: Brain executor has `pc` (program counter) → map `pc` to block index → send a command or state so the strip highlights that position (e.g. brighter, different color, or a small animation). Reuse the same LED driver calls as in `led_ux` and the LED Color Flash block (e.g. set a range of pixels to a color, then `matrix_show()`).

---

### 7. Brain broadcasts: make matrix + speaker behave via event handler

- **What:** The Brain block should also function with **matrix** and **speaker** outputs based on the event handler (same “broadcast” path as other peripherals, not one-off demo code paths).
- **Where:** Brain event handler + whatever “broadcast”/message routing exists in Brain firmware (search around `brain_event_handler.c`, executor tick, and any peripheral dispatch utilities).
- **Branch:** Create a focused branch (or extend the branch that owns the event-handler/peripheral work).
- **Owner:** TBD (likely Jordan for event-handler plumbing; Destiny for peripherals UX).
- **Status:** [ ] Not started
- **How:**
  1. **Single source of truth:** Ensure the event handler is the one place that interprets run-state + “current block” and decides what to broadcast (e.g. RUNNING/IDLE/ERROR + current `pc`).
  2. **Matrix parity:** When the Brain is running, the matrix should reflect the same execution state that the LED strip/mirroring expects (e.g. highlight current step, show running animation, etc.).
  3. **Speaker parity:** When the Brain broadcasts a “run start / step / done / error” (or similar), the speaker should respond consistently (e.g. startup jingle, tick, completion tone) using the shared audio component if available.

---

### 8. Broadcast mirroring parity across all blocks (matrix + speaker + LED)

- **What:** Broadcasts originating from the Brain should **mirror consistently** on *all* blocks/peripherals that support it (matrix, speaker, LED strip/LED matrix), so behavior feels unified.
- **Where:** Brain broadcast message format + child-block command handlers (`CMD_EXECUTE` handling, plus any “broadcast”/“set visuals” command), shared `led_ux` / shared audio.
- **Branch:** Could live with Task #6 (LED mirroring) or as a dedicated “broadcast-parity” branch.
- **Owner:** TBD
- **Status:** [ ] Not started
- **How:**
  1. **Define expected behavior:** For each broadcast type (idle, running, step highlight, ok/done, error/stop), define what matrix shows, what LEDs show, and what speaker plays.
  2. **Standardize the transport:** Prefer one shared command/message type that every block can interpret (or a small set) rather than bespoke per-block commands.
  3. **Implement in each block template:** Ensure each block’s `command_handle()` responds the same way for the shared broadcast(s), even if it’s a minimal/no-op for hardware it doesn’t have.

---

### 9. Update any docs

- **What:** General doc pass: accuracy, links, and consistency (e.g. block inventory, firmware-api, architecture, getting-started).
- **Where:** `docs/` (all `.md` and related).
- **Branch:** Can go on `protocol-docs-consistency` or a small `docs-update` branch.
- **Owner:** **Jordan** and **Destiny** (split by area or pass together).
- **Status:** [ ] In progress (recent updates landed in `docs/hardware/block-inventory.md` and `docs/midterm-demo/slides.html`)
- **How:**
  1. **List of docs to touch:** `docs/README.md`, `docs/getting-started/overview.md`, `docs/getting-started/app-setup.md`, `docs/getting-started/firmware-setup.md`, `docs/architecture/system-overview.md`, `docs/architecture/firmware-architecture.md`, `docs/architecture/app-architecture.md`, `docs/api/firmware-api.md`, `docs/api/app-api.md`, `docs/hardware/block-inventory.md`. Plus any in `docs/midterm-demo/` if still relevant.
  2. **What to do:** Open each and check: (a) links to other docs or to code paths are correct; (b) block counts, GPIO pins, I2C addresses, command names match `i2c_protocol.h` and current firmware; (c) “getting started” steps still work; (d) no outdated branch or feature names. Split the list between you and Jordan (e.g. Jordan: firmware-api, architecture; Destiny: block-inventory, getting-started) and do one pass each, then cross-check.

---

### 10. GPIO pinout markdown

- **What:** Add a markdown doc that describes GPIO pinouts (e.g. I2C 21/22, TFT, LED, speaker, etc.).
- **Where:** `docs/hardware/` (suggested: `docs/hardware/gpio-pinouts.md`); `docs/hardware/block-inventory.md` already mentions GPIO 21/22.
- **Branch:** `main` (doc + PCB files are now committed directly on main).
- **Owner:** Jordan 
- **Status:** [x] Done
- **How:**
  1. **GPIO doc created:** `docs/hardware/gpio-pinouts.md` is the single source of truth for ESP32 pin usage across Brain + all child blocks. It documents I2C (21/22), TFT, LED matrix, LED strip, speaker DAC, SPI, touch CS, and status LEDs.
  2. **Backed by PCB files:** Hardware design zips (`Main_PCB.zip`, `Amplifier.zip`, `ChargingModule.zip`, `VoltageRegulators.zip`) live in `pcb_files/`, with `pcb_files/README.md` explaining contents and pointing back to `gpio-pinouts.md` so firmware and PCB stay in sync.

---

### 11. Battery percentage on each block’s TFT UI

- **What:** Show battery percentage on the TFT UI of each child block that has a display (and battery monitoring).
- **Where:** Child block templates with TFT (e.g. blocks that use a display); `firmware_blocks/block_templates/common_block/components/battery_monitor` if present; each block’s UI code.
- **Branch:** New branch (e.g. `block-tft-battery`) or add to a block-UI branch if one exists.
- **Owner:** **Destiny** (displays / peripherals).
- **Status:** [X] Implemented + merged to `main` Done
- **How:**
  1. **Which blocks have a TFT:** Only **LED Color Flash** and **Music Sequence** have a TFT (`main/tft_ui.c`). Add a small battery % label (e.g. corner) in each block's UI. Other blocks have no TFT yet.
  2. **Where battery data comes from:** Check `common_block/components/battery_monitor`. You need an API like `battery_monitor_get_percent()`. If not implemented, stub it (e.g. return 100) so the UI can be built; Jordan/hardware can help with real ADC wiring later.
  3. **In the TFT UI:** In `tft_ui.c`, add a label for the percentage. Periodically (e.g. every few seconds) call the battery API and set the label text (e.g. `lv_label_set_text_fmt(battery_label, "%u%%", percent)`). Run that from the existing LVGL task or timer.
  4. **Control flow blocks:** They only have an LED matrix today, so battery-on-TFT applies only once they get a display. Focus on LED Color Flash and Music Sequence first.



---

### 12. Control flow blocks TFT UI: block type label + disco animations

- **What:** (a) Each control flow block’s TFT UI should clearly show what kind of block it is (e.g. “If”, “Then”, “End If”, “Loop”, “End Loop”, “Delay”). (b) When that block is executing, the UI should switch to fun disco-style animations (e.g. colors, motion) so execution is visible and engaging.
- **Where:** Control flow block templates with TFT: `if_block`, `then_block`, `end_if_block`, `loop_block`, `end_loop_block`, `delay_block` under `firmware_blocks/block_templates/`; each block’s display/UI code and execution-state handling (e.g. when Brain sends CMD_EXECUTE or block is “active” in the run).
- **Branch:** Same as #4 (`control-flow-docs-and-blocks`) or a dedicated branch (e.g. `control-flow-block-tft-ui`). Coordinates with execution behavior from #4.
- **Owner:** **Destiny** (displays / “what you see”).
- **Status:** [X] In progress (will then merge `led-strip-behavior` into `main`) 
- **How:**
  1. **Which blocks have a TFT today:** Control flow blocks (if, then, end_if, loop, end_loop, delay) currently use **LED matrix** only (see their `main.c`: `led_matrix_init()`, `led_matrix_startup_animation()`). So either (a) add a small TFT to those blocks later and show the label + disco there, or (b) for now use the **LED matrix** to show the block type (e.g. scrolling "LOOP" or a color) and disco = animations on the matrix (like `led_ux_show_running()` but fancier).
  2. **Block type label:** Each control flow block's `main.c` already has `BLOCK_NAME` (e.g. "LOOP", "IF"). If using TFT: add a small display driver and draw that string on screen (reference: `led_color_flash_block/main/tft_ui.c` for LVGL + display init). If using LED matrix only: drive the matrix to show the text or a distinct pattern per block type (see `led_matrix.c` / pattern helpers in LED Color Flash block).
  3. **Disco when executing:** When the Brain sends **CMD_EXECUTE** to that block (i.e. the executor is on that step), the block should switch from idle to running visuals. In `command_handle()`, when you get `CMD_EXECUTE`, call `peripherals_show_running()` — which is currently a stub in each control flow block. Implement it: e.g. start a short disco animation (flashing colors, moving pattern) on the LED matrix (or TFT). Use a timer or a small task so the animation runs for a second or two, then stop. Reuse ideas from `led_ux_show_running()` or the LED Color Flash block's pattern code.
  4. **Keeping executing in sync:** The block only knows it's executing when it receives CMD_EXECUTE. The disco starts on that command and can auto-stop after a fixed time, or when the block gets CMD_RESET. No need to talk back to the Brain — just local reaction to I2C commands.

---

## Summary table

| # | Task | Suggested owner | Branch | Status |
|---|------|-----------------|--------|--------|
| 1 | Brain Stop button (Start→Stop) | Destiny | `main` | [✓] Done (merged to `main`) |
| 2 | Note block firmware | Jordan | `note-block-firmware` (local) | [x] Implemented (needs merge/cherry-pick) |
| 3 | Protocol docs + startup sound | Jordan | merged to `main` | [✓] Done |
| 4 | Control flow blocks (write out) | Jordan | `origin/control-flow-docs-and-blocks` | [x] Implemented on branch (needs merge) |
| 5 | Music sequence block + more songs | Destiny | `main` |  [✓] Done (merged to `main`) |
| 6 | LED strip idle + execution mirroring | Destiny | `led-strip-behavior` | [ ] In progress |
| 7 | Brain broadcasts: matrix + speaker via event handler | TBD | new | [ ] Not started |
| 8 | Broadcast mirroring parity across blocks | TBD | new or with #6 | [ ] Not started |
| 9 | Update any docs | Jordan | same as #3 or small branch | [ ] In progress |
| 10 | GPIO pinout markdown | Jordan / hardware owner | `main` (`docs/hardware/gpio-pinouts.md`) | [✓] Done |
| 11 | Battery % on each block TFT UI | Destiny | new (e.g. `block-tft-battery`) |  [✓] Done (merged to `main`) | On MSQ + LCF Blocks
| 12 | Control flow TFT: block label + disco on execution | Destiny | `origin/control-flow-docs-and-blocks` or `origin/led-strip-behavior` | [ ] In progress (plan to merge into `led-strip-behavior`) |

---

*Edit the “Owner” lines and checkboxes as you assign and complete. Last updated: 2026-03-23 (audited from local `main` + current remote refs).*
