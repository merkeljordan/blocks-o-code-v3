# Broadcast Execution Semantics (Decision Spec)

**Status:** Updated for sequential action + shared runtime broadcast  
**Date:** 2026-04-03  
**Owners:** Firmware + App

## 1) Purpose

Define the canonical runtime semantics for Brain executor behavior so output timing, `IF`, `LOOP`, `DELAY`, and `BUTTON` wait are deterministic and testable.

## 2) Scope

Applies to Brain runtime execution in:
- `brain_event_handler.c` executor tick + dispatch behavior
- I2C command fan-out from Brain to child blocks

Out of scope:
- physical-bus hard real-time synchronization
- app-side config validation rules

## 3) Normative Rules

### 3.1 Execution model

1. The Brain **MUST** execute a single program counter (`pc`) over the program snapshot captured at `START` (types + I2C addresses + present flags).
2. The executor **MUST** be tick-driven and non-blocking.
3. For any output step (`LED_FLASH`, `NOTE`, `MUSIC_SEQ`), the Brain **MUST** send **action** I2C only to the child at program index `pc` when that slot is present and matches the step type.
4. The system **MUST NOT** require true simultaneous wire-time execution across blocks.
5. While a run is active, committed `block_config_manager` state **MAY** update from scans; the executor **MUST NOT** stop the run solely for that reason. The in-flight run **MUST** keep using the `START` snapshot until it ends.

### 3.2 Shared UX + sequential action

1. On output steps, the Brain **MUST** still emit `CMD_RUNTIME_BROADCAST` to **all** present blocks for strip/matrix/speaker parity.
2. For `LED_FLASH` at `pc`, the Brain **MUST** push `color_id` to that target only, then `CMD_EXECUTE` to that target only (per-`pc` submit when available).
3. For `NOTE` at `pc`, the Brain **MUST** send `CMD_PLAY_NOTE` to that target only, using that block’s Brain-stored submit: **one** command for a single-note submit, or **one command per note in order** for a sequence submit, waiting each note to complete (child `STATUS_BUSY` / bounded timing) before sending the next. The executor **MUST NOT** advance past the NOTE step until the sequence is finished or `STOP` is requested. The Brain **MUST NOT** broadcast `CMD_EXECUTE` for NOTE solely for audio (matrix feedback may use runtime broadcast).
4. For `MUSIC_SEQ` at `pc`, the Brain **MUST** send `CMD_EXECUTE` to that target only (local song selection on the block).
5. The Brain **SHOULD** continue targeted dispatch even if the target returns an I2C error.
6. The Brain **MUST** log trigger step type and dispatch results.

### 3.2a Shared runtime parity transport

1. The Brain **MUST** emit `CMD_RUNTIME_BROADCAST` from the event handler as the shared UX transport for runtime parity.
2. `CMD_RUNTIME_BROADCAST` **MUST** carry:
   - runtime state (`IDLE`, `RUNNING`, `STEP`, `DONE`, `ERROR`, `STOP`)
   - current highlight `pc` (or sentinel when not applicable), aligned with the opcode holding `pc` during `DELAY` / `BUTTON` wait
   - current `block_type_t` step type when relevant
3. Child blocks **MUST** interpret `CMD_RUNTIME_BROADCAST` as synchronized visual/audio state and **MUST NOT** treat it as a substitute for action commands on other blocks.
4. Blocks that lack a given peripheral **MUST** degrade to a safe no-op for that part of the contract.

### 3.3 Delay semantics

1. `DELAY` **MUST** use a shared monotonic Brain timebase (`now_ms`) with `wait_until_ms`.
2. `DELAY` duration **MUST** resolve as:
   - per-program-position submitted value (if present), else
   - executor default `delay_ms`.
3. While waiting, executor state **MUST** be `EXECUTOR_WAIT_DELAY`, `pc` **MUST** remain on the `DELAY` opcode, and the runtime highlight **MUST** match that `pc`.
4. When the delay elapses, the executor **MUST** advance `pc` past the delay.

### 3.4 IF semantics

1. Canonical sequence **MUST** be `IF` → `BUTTON` → `THEN` → outputs → `END_IF`.
2. At `IF`, the executor **MUST NOT** evaluate the condition yet; it **MUST** record the matching `END_IF` and `THEN` indices, bind the BUTTON at `program[if_pc+1]` when that opcode is `BLOCK_TYPE_BUTTON` and lies before `THEN`, push an IF frame (bounded depth), and advance `pc` into the `BUTTON` step.
3. The `BUTTON` step **MUST** use `EXECUTOR_WAIT_INPUT` until that bound block’s address reports a press; a qualifying press **MUST** update the shared press latch used at `THEN`.
4. At `THEN`, the executor **MUST** compare the latch to the bound BUTTON address from the `START` snapshot, **MUST** consume the latch on that evaluation, and **MUST** either jump to `END_IF + 1` (false) or continue after `THEN` (true).
5. While blocked in `EXECUTOR_WAIT_INPUT` on a BUTTON step, a press from a **different** address **MUST NOT** update the IF latch (so stray presses do not arm `THEN`).
6. At `END_IF`, the executor **MUST** pop the IF frame when the program counter matches the stacked `END_IF` index (supports nesting up to the bound stack depth).

### 3.4a BUTTON wait step

1. When `pc` is `BLOCK_TYPE_BUTTON`, the executor **MUST** enter `EXECUTOR_WAIT_INPUT` until `BRAIN_BLOCK_EVENT_BUTTON_PRESS` from that slot’s I2C address, then advance `pc`.

### 3.5 LOOP semantics

1. On `LOOP`, executor **MUST** find matching `END_LOOP` with nesting support.
2. The **loop body** **MUST** be the contiguous program indices **strictly between** `LOOP` and that `END_LOOP` (i.e. from `loop_pc + 1` through `end_loop_pc - 1`). Only those opcodes **MUST** be re-run each iteration; `LOOP` and `END_LOOP` **MUST NOT** be treated as body steps.
3. Loop iteration count **MUST** resolve as:
   - per-program-position submitted value (if present), else
   - executor default `loop_count` (minimum 1).
4. Executor **MUST** manage nested loops via a bounded loop stack.
5. After entering `LOOP`, executor **MUST** set `pc` to `loop_start_pc + 1` (first body opcode).
6. At `END_LOOP`, executor **MUST** jump back to `loop_start_pc + 1` while iterations remain, or pop the frame and continue after `END_LOOP` when the last iteration completes.

### 3.6 Concurrency

1. Mutations of executor context, IF latch, wait flags, and shared executor params **MUST** be serialized with the Brain’s executor mutex (recursive) across `brain_executor_tick()` and block-event handling.

## 4) Determinism + Timing Guarantees

1. Program behavior **MUST** be deterministic for a fixed `START` snapshot and identical input events.
2. Inter-block output skew is expected on I2C and **MUST** be treated as bounded transport latency, not semantic divergence.
3. `STOP` **MUST** preempt future execution by transitioning to stopped state on next tick.

## 5) Examples (Normative)

### Example A: Sequential output + broadcast UX

Program:
`LED_FLASH -> NOTE -> MUSIC_SEQ`

- At each step, only the block at `pc` receives the action command (`CMD_EXECUTE` / `CMD_PLAY_NOTE` as applicable).
- All present blocks still receive each `CMD_RUNTIME_BROADCAST` for highlight/parity.

### Example B: Delay inside loop

Program:
`LOOP(3) -> LED_FLASH -> DELAY(200ms) -> END_LOOP`

Behavior:
- LED runs once per iteration (single target per visit).
- Each iteration holds `pc` on `DELAY` until 200 ms elapses, then advances.
- Loop exits after third `END_LOOP` visit.

### Example C: IF false path

Program:
`IF -> BUTTON -> THEN -> MUSIC_SEQ -> END_IF -> NOTE`

If condition false at `THEN` (after the `BUTTON` wait):
- Jump to `END_IF + 1`.
- `MUSIC_SEQ` body is skipped.
- `NOTE` still runs at that `pc` (one or more `CMD_PLAY_NOTE` per stored submit) when reached.

### Example D: IF → BUTTON → THEN

Program:
`IF -> BUTTON -> THEN -> NOTE -> END_IF`

- `IF` enters the conditional region; `BUTTON` waits (`EXECUTOR_WAIT_INPUT`).
- At `THEN`, the latch must match the BUTTON immediately after `IF`; if true, `NOTE` runs; if false, jump past `END_IF`.

## 6) Risks + Mitigations

- Risk: users perceive non-zero skew between blocks.
  - Mitigation: deterministic order + clear UX expectation + `CMD_RUNTIME_BROADCAST` for shared parity.
- Risk: one failing output block interrupts others.
  - Mitigation: continue dispatch on error and report per-target failures.

## 7) Acceptance Criteria

1. Output steps target only the block at `pc` for action I2C; runtime broadcast still fans out.
2. `IF`/`LOOP`/`DELAY`/`BUTTON` wait behaviors remain correct per sections 3.3–3.5.
3. No blocking waits inside executor tick (except state transitions driven by time/input).
4. Logs and tests reflect sequential action + broadcast UX semantics.
5. Matrix, status-strip, and speaker-capable blocks respond through the shared `CMD_RUNTIME_BROADCAST` path with semantically consistent parity UX.
