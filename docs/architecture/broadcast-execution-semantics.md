# Broadcast Execution Semantics (Decision Spec)

**Status:** Proposed for team sign-off  
**Date:** 2026-03-31  
**Owners:** Firmware + App

## 1) Purpose

Define the canonical runtime semantics for Brain executor broadcast behavior so output timing, `IF`, `LOOP`, and `DELAY` are deterministic and testable.

## 2) Scope

Applies to Brain runtime execution in:
- `brain_event_handler.c` executor tick + dispatch behavior
- I2C command fan-out from Brain to child blocks

Out of scope:
- physical-bus hard real-time synchronization
- app-side config validation rules

## 3) Normative Rules

### 3.1 Execution model

1. The Brain **MUST** execute a single program counter (`pc`) over the scanned program list.
2. The executor **MUST** be tick-driven and non-blocking.
3. For any output step (`LED_FLASH`, `NOTE`, `MUSIC_SEQ`), the Brain **MUST** broadcast `CMD_EXECUTE` to **all present blocks** in deterministic config order.
4. The system **MUST NOT** require true simultaneous wire-time execution across blocks.

### 3.2 Broadcast output behavior

1. Before output broadcast, the Brain **MUST** push class-specific config needed by receivers (for example, `LED_FLASH` color ID to all blocks).
2. During an output step, the Brain **MUST** send `CMD_EXECUTE` to each present block (including `LED_FLASH`, `NOTE`, `MUSIC_SEQ`, `IF`, etc.).
3. The Brain **SHOULD** continue fan-out even if one target block returns an I2C error.
4. The Brain **MUST** log trigger step type and fan-out success counts.

### 3.2a Shared runtime parity transport

1. The Brain **MUST** emit `CMD_RUNTIME_BROADCAST` from the event handler as the shared UX transport for runtime parity.
2. `CMD_RUNTIME_BROADCAST` **MUST** carry:
   - runtime state (`IDLE`, `RUNNING`, `STEP`, `DONE`, `ERROR`, `STOP`)
   - current highlight `pc` (or sentinel when not applicable)
   - current step `block_type_t` when relevant
3. Child blocks **MUST** interpret `CMD_RUNTIME_BROADCAST` as synchronized visual/audio state and **MUST NOT** treat it as a substitute for `CMD_EXECUTE`.
4. Blocks that lack a given peripheral **MUST** degrade to a safe no-op for that part of the contract.

### 3.3 Delay semantics

1. `DELAY` **MUST** use a shared monotonic Brain timebase (`now_ms`) with `wait_until_ms`.
2. `DELAY` duration **MUST** resolve as:
   - per-program-position submitted value (if present), else
   - executor default `delay_ms`.
3. While waiting, executor state **MUST** be `EXECUTOR_WAIT_DELAY` and **MUST NOT** advance `pc`.

### 3.4 IF semantics

1. `IF` condition source **MUST** be the current executor context (`button_pressed`).
2. Condition evaluation **MUST** occur when `pc` reaches the `IF` instruction (per-block-context evaluation).
3. If condition is false, executor **MUST** jump to matching `END_IF + 1`.
4. If condition is true, executor **MUST** consume the condition event (single-press semantics) and execute body.

### 3.5 LOOP semantics

1. On `LOOP`, executor **MUST** find matching `END_LOOP` with nesting support.
2. Loop iteration count **MUST** resolve as:
   - per-program-position submitted value (if present), else
   - executor default `loop_count` (minimum 1).
3. Executor **MUST** manage nested loops via a bounded loop stack.
4. At `END_LOOP`, executor **MUST** jump back to loop body until iterations are exhausted; then continue after `END_LOOP`.

## 4) Determinism + Timing Guarantees

1. Program behavior **MUST** be deterministic for a fixed scanned block order and identical input events.
2. Inter-block output skew is expected on I2C and **MUST** be treated as bounded transport latency, not semantic divergence.
3. `STOP` **MUST** preempt future execution by transitioning to stopped state on next tick.

## 5) Examples (Normative)

### Example A: Output fan-out

Program:
`BUTTON -> THEN -> NOTE`

If button condition is true at `IF`/gate point:
- `NOTE` step triggers `CMD_EXECUTE` to all present blocks:
  - ALL blocks execute

### Example B: Delay inside loop

Program:
`LOOP(3) -> LED_FLASH -> DELAY(200ms) -> END_LOOP`

Behavior:
- LED/output fan-out runs three times.
- Each iteration waits 200 ms (per-position override if submitted).
- Loop exits after third `END_LOOP` visit.

### Example C: IF false path

Program:
`IF -> THEN -> MUSIC_SEQ -> END_IF -> NOTE`

If condition false at `IF`:
- Jump to `END_IF + 1`.
- `MUSIC_SEQ` body is skipped.
- `NOTE` still runs and fans out to all blocks.

## 6) Risks + Mitigations

- Risk: users perceive non-zero skew between blocks.
  - Mitigation: deterministic order + clear UX expectation + optional future two-phase sync extension.
- Risk: one failing output block interrupts others.
  - Mitigation: continue fan-out on error and report per-target failures.

## 7) Acceptance Criteria

1. Output steps fan-out to all present blocks.
2. Existing `IF`/`LOOP`/`DELAY` behaviors remain correct.
3. No blocking waits inside executor tick (except state transitions driven by time/input).
4. Logs and tests reflect broadcast fan-out semantics.
5. Matrix, status-strip, and speaker-capable blocks respond through the shared `CMD_RUNTIME_BROADCAST` path with semantically consistent parity UX.
