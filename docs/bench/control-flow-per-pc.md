# Control-flow per-`pc` overrides bench scenarios

This repo’s Brain executor supports **per-program-position (`pc`) overrides** for:
- `LOOP` → loop count (from `BRAIN_BLOCK_EVENT_LOOP_COUNT_SUBMIT`)
- `DELAY` → delay milliseconds (from `BRAIN_BLOCK_EVENT_DELAY_MS_SUBMIT`)

The Brain consumes these overrides via:
- Child sets `REG_STATUS` flag `STATUS_DATA_READY`
- Brain sends `CMD_GET_DATA`
- Child returns: `[event_id][payload…]` and clears `STATUS_DATA_READY`

## Scenario 1: Two DELAY blocks, different overrides

**Physical block order (excluding Brain):**

`DELAY_A → OUTPUT → DELAY_B → OUTPUT`

**Steps**
- Power chain and wait for initial scan.
- Confirm each DELAY publishes its default once (Brain logs `DELAY_MS submit ... pc=<pc>` twice at two different `pc`s).
- Change only DELAY_B’s delay (e.g. via whatever UI the block has, or by sending `CMD_SET_DELAY` if you’re doing I2C bench control).
- Confirm Brain logs a new `DELAY_MS submit ... pc=<pc_of_DELAY_B>` and that only that `pc` is updated.

**Expected**
- `brain_event_handler.c` should show:
  - one `pc` using `s_delay_ms_by_pc[pc] = delay_A`
  - one `pc` using `s_delay_ms_by_pc[pc] = delay_B`
- Executor should wait different amounts at each DELAY step.

## Scenario 2: Nested loops with distinct counts

**Physical block order:**

`LOOP_OUTER → OUTPUT → LOOP_INNER → OUTPUT → END_LOOP → OUTPUT → END_LOOP`

**Steps**
- Power chain and wait for initial scan.
- Confirm each LOOP publishes its default once (Brain logs `LOOP_COUNT submit ... pc=<pc>` for both loop PCs).
- Change outer loop count (e.g. 2) and inner loop count (e.g. 3).
- Start execution.

**Expected**
- Brain logs two distinct `LOOP_COUNT submit` updates with different `pc`s.
- Runtime behavior corresponds to nested iteration:
  - inner body runs 3× per outer iteration
  - outer body runs 2× total

## Scenario 3: Config change clears overrides

**Physical block order:**

`LOOP → OUTPUT → END_LOOP`

**Steps**
- Set loop count to a non-default value and confirm Brain logs the override.
- Physically reorder blocks or add/remove a block to force a scan/topology change.
- Confirm Brain logs config refresh. If a run was active, the executor should **continue** with the program snapshot from `START` until DONE/STOP/ERROR; the next `START` should follow the newly committed topology.

**Expected**
- Per-`pc` tables are rebuilt on the next `START` from the latest scan + stash state.
- On the new topology, LOOP should re-submit defaults (or be reconfigured) so overrides are repopulated.

