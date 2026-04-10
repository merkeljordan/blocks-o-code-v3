# Firmware API Reference

This document provides API reference for the ESP32 Brain Block firmware.

Related decision spec: [Broadcast Execution Semantics](../architecture/broadcast-execution-semantics.md)

## App -> Brain Events

### `config_validation`

The companion app sends this newline-delimited JSON event after each configuration
validation result and when reconnecting to the Brain Block.

**Contract**:
```json
{
  "type": "config_validation",
  "is_valid": true,
  "error_count": 0
}
```

**Fields**:
- `type` (string, required): Must be `"config_validation"`.
- `is_valid` (boolean, required): Whether the app's latest config validation passed.
- `error_count` (number, optional): Number of validation errors from the app.
- `timestamp` (number, optional): App timestamp in milliseconds.

**Behavior**:
- Brain defaults to **invalid** until at least one valid event is received.
- Brain should block `START` requests when invalid and respond with `NAK:INVALID_CONFIG`.
- Brain should keep `STOP` always allowed.
- Brain should reset validation state to invalid on TCP disconnect.

## Runtime Command Responses

The Brain TCP command channel uses newline-delimited plain text acknowledgements:

- `START` -> `ACK:START` when config is valid and execution starts.
- `START` -> `NAK:INVALID_CONFIG` when validation gate is not satisfied.
- `START` -> `NAK:INVALID_STATE` when executor cannot start (for example no runnable program).
- `STOP` -> `ACK:STOP` (always accepted).
- Unknown command -> `NAK:UNKNOWN`.

## Brain Event Handler + Executor

### Validation Gate API (`brain_event_handler.h`)

```c
void brain_event_handler_init(void);
void brain_event_handler_reset_validation(void);
void brain_event_handler_set_config_validation(bool is_valid, uint32_t error_count, uint64_t timestamp_ms);
const brain_validation_state_t *brain_event_handler_get_validation_state(void);
bool brain_event_handler_can_start_execution(void);
```

### Scan-Derived Event Map

The block configuration manager exposes metadata derived from scanned block topology:

```c
const block_event_map_t* block_config_manager_get_event_map(void);
```

`block_event_map_t` summarizes:
- IF/LOOP control-start and boundary counts
- sequence boundaries
- presence/count of input blocks inside IF sequences
- presence/count of output-or-delay blocks inside sequence bodies

### Executor API

```c
void brain_executor_set_params(const brain_executor_params_t *params);
const brain_executor_context_t *brain_executor_get_context(void);
void brain_executor_set_button_state(bool is_pressed);
esp_err_t brain_executor_start(void);
void brain_executor_stop(void);
void brain_executor_tick(void);
bool brain_executor_prefers_i2c_yield(void);
```

Executor state enum:
- `EXECUTOR_IDLE`
- `EXECUTOR_RUNNING`
- `EXECUTOR_WAIT_INPUT`
- `EXECUTOR_WAIT_DELAY`
- `EXECUTOR_STOPPED`
- `EXECUTOR_DONE`

Notes:
- Executor is tick-driven (periodic task). Control flow steps are non-blocking; output steps that wait on children (e.g. NOTE sequence, MUSIC busy) block within the tick until complete or `STOP`.
- IF/LOOP/DELAY/BUTTON wait control flow is interpreted with a program counter.
- I2C topology scans may commit new `block_config_manager` state while a run is in progress; the executor keeps using the program snapshot and per-block I2C addresses captured at `START` until the run ends (STOP, DONE, or ERROR). Stale topology during a run is intentional; the next `START` picks up the latest scan.
- A FreeRTOS recursive mutex serializes `brain_executor_tick()` and block-event handling paths that mutate executor context, button/IF latch state, and shared params.
- **Bus sharing:** All Brain I²C uses a recursive master mutex. While `brain_executor_prefers_i2c_yield()` is true (`EXECUTOR_RUNNING`, `EXECUTOR_WAIT_DELAY`, `EXECUTOR_WAIT_INPUT`), firmware **backs off** background traffic: the block-config scan task in `app.c` sleeps longer between scans (~450 ms vs ~50 ms idle), and the block event poll task in `main.c` sleeps longer between rounds (~120 ms vs ~40 ms idle), so executor dispatch spends less time waiting behind scans/polls.
- **STOP:** `brain_event_handle_message("STOP")` sets `stop_requested` **before** waiting on the executor mutex, and `brain_executor_stop()` sets it without taking that mutex, so long NOTE playback (mutex held for the whole step) still sees STOP. The flag is `volatile` so wait loops observe it immediately.

### Control Flow Execution Semantics (IF / LOOP / DELAY)

The Brain executes a “program” that is the scanned block list (excluding the Brain). The executor holds:
- `program[]`: array of `block_type_t`
- `pc`: program counter (index into `program[]`)
- `loop_stack[]`: loop frames for nested loops

At each tick (`brain_executor_tick()`), the executor advances `pc` or transitions into a wait state.

**Output steps (sequential action, shared UX broadcast)**:
- For `LED_FLASH`, `NOTE`, and `MUSIC_SEQ` steps, the Brain sends **action** I2C only to the child at the current program index `pc` (from the `START` snapshot): `i2c_set_led_color_id` + `CMD_EXECUTE` for LED; for NOTE, one or more `CMD_PLAY_NOTE` commands—**one note** if the user submitted a single note, or **the full sequence in order** if they submitted a sequence (each note is waited out via the child `STATUS_BUSY` / timing before the next, and `pc` does not advance until the step finishes or `STOP` interrupts); matrix/speaker strip parity still follows `CMD_RUNTIME_BROADCAST`; `CMD_EXECUTE` for `MUSIC_SEQ` (that block’s locally selected song).
- Separately, the Brain still fans out `CMD_RUNTIME_BROADCAST` to **all** present children for strip/matrix/speaker parity.

### Shared Runtime Broadcast Contract

The Brain also emits `CMD_RUNTIME_BROADCAST` for synchronized cross-peripheral UX parity.

Wire payload:
- `byte0`: `brain_runtime_broadcast_state_t`
- `byte1`: highlighted `pc` (or `BRAIN_RUNTIME_PC_NONE`)
- `byte2`: current `block_type_t` step type (or `BLOCK_TYPE_UNKNOWN`)

Defined states:
- `BRAIN_RUNTIME_IDLE`
- `BRAIN_RUNTIME_RUNNING`
- `BRAIN_RUNTIME_STEP`
- `BRAIN_RUNTIME_DONE`
- `BRAIN_RUNTIME_ERROR`
- `BRAIN_RUNTIME_STOP`

Behavior:
- Brain event handler is the single source of truth for when runtime broadcasts are emitted.
- Child blocks treat `CMD_RUNTIME_BROADCAST` as shared UX state, not as an action-execution command.
- Action commands are addressed to the current `pc` target only (`CMD_EXECUTE`, `CMD_PLAY_NOTE`, LED color + execute, etc.).
- Blocks without a relevant peripheral should no-op safely.

**Optional future extension (not default):**
- A two-phase sync pattern (`PREPARE` then short-window `GO`) may be added in a future protocol revision for tighter perceived simultaneity across target blocks.
- Current/default behavior remains deterministic batched fan-out over standard I2C command dispatch.

### Shared Runtime Broadcast Contract

The Brain also emits `CMD_RUNTIME_BROADCAST` for synchronized cross-peripheral UX parity.

Wire payload:
- `byte0`: `brain_runtime_broadcast_state_t`
- `byte1`: highlighted `pc` (or `BRAIN_RUNTIME_PC_NONE`)
- `byte2`: current `block_type_t` step type (or `BLOCK_TYPE_UNKNOWN`)

Defined states:
- `BRAIN_RUNTIME_IDLE`
- `BRAIN_RUNTIME_RUNNING`
- `BRAIN_RUNTIME_STEP`
- `BRAIN_RUNTIME_DONE`
- `BRAIN_RUNTIME_ERROR`
- `BRAIN_RUNTIME_STOP`

Behavior:
- Brain event handler is the single source of truth for when runtime broadcasts are emitted.
- Child blocks treat `CMD_RUNTIME_BROADCAST` as shared UX state, not as an action-execution command.
- `CMD_EXECUTE` still performs the actual block action.
- Blocks without a relevant peripheral should no-op safely.

**Optional future extension (not default):**
- A two-phase sync pattern (`PREPARE` then short-window `GO`) may be added in a future protocol revision for tighter perceived simultaneity across target blocks.
- Current/default behavior remains deterministic batched fan-out over standard I2C command dispatch.

**DELAY** (`BLOCK_TYPE_DELAY`):
- On a DELAY step, the executor sets `wait_until_ms = now + delay_ms`, enters `EXECUTOR_WAIT_DELAY`, and **keeps `pc` on the DELAY opcode** until the wait completes.
- When the delay expires, the executor advances `pc` past the delay and resumes.
- `delay_ms` is chosen per-program-position when available (see “Block -> Brain control-flow config submit” below). Otherwise it falls back to the executor global parameter `brain_executor_params_t.delay_ms`.

**BUTTON** (`BLOCK_TYPE_BUTTON`):
- Treated as an input step: the executor issues `CMD_EXECUTE` once to that block (local feedback), enters `EXECUTOR_WAIT_INPUT`, and does not advance `pc` until a `BRAIN_BLOCK_EVENT_BUTTON_PRESS` arrives from **that** block’s I2C address. `CMD_RUNTIME_BROADCAST` continues to use `RUNNING` + the BUTTON step type so children show the waiting/active step.

**IF / THEN / END_IF** (`BLOCK_TYPE_IF`, `BLOCK_TYPE_THEN`, `BLOCK_TYPE_END_IF`):
- Canonical physical/program order: **`IF` → `BUTTON` → `THEN` → outputs → `END_IF`**. The executor enters the `IF` by pushing a small frame and advancing to the `BUTTON` step (no condition yet). The `BUTTON` step uses `EXECUTOR_WAIT_INPUT` until that block’s press (latching the source address). At **`THEN`**, the executor evaluates the condition: the latch must match the **bound** BUTTON, which is the block **immediately after `IF`** (`program[if_pc+1]` must be `BLOCK_TYPE_BUTTON`, and it must lie before the matching `THEN`). Other button blocks do not satisfy the condition. The latch is consumed when `THEN` is evaluated.
- When the condition is false at `THEN`, the executor jumps to the matching `END_IF + 1` (skipping the output body) and pops the IF frame.
- When the condition is true, execution continues with the steps after `THEN` until the matching `END_IF`; `END_IF` pops the frame for that block. Nested `IF`s use a bounded IF stack (depth 4).

**LOOP / END_LOOP** (`BLOCK_TYPE_LOOP`, `BLOCK_TYPE_END_LOOP`):
- The **loop body** is only the program slots **between** the matching pair: indices `(loop_pc + 1) … (end_loop_pc - 1)` inclusive. Those steps—outputs, delays, nested control flow, etc.—are what repeat; the `LOOP` and `END_LOOP` blocks themselves are boundaries and are not part of the repeated body.
- On `LOOP`, the executor finds the matching `END_LOOP` (accounting for nesting) and pushes a loop frame:
  - `loop_start_pc`: the `LOOP` index
  - `loop_end_pc`: the `END_LOOP` index
  - `remaining_iterations`: `loop_count` (per-program-position when available; otherwise from executor params; minimum 1)
- The executor then advances into the body (`pc = loop_start_pc + 1`).
- When `pc` reaches `END_LOOP`, it either jumps back to `loop_start_pc + 1` for another full pass through the body while iterations remain, or pops the loop frame and continues with the step **after** `END_LOOP`.
- Iteration counts from the loop block (register / submit) are **capped at 64** to avoid runaway programs if the bus returns garbage or an extreme UI value.

### Control Flow Child UX (TFT + Status Strip)

Control-flow child blocks (`IF`, `THEN`, `END_IF`, `LOOP`, `END_LOOP`, `DELAY`) expose their execution state locally in two ways:

- TFT UI: each block boots into an idle screen that shows its block-type label and block-specific accent styling.
- Execute feedback: when the Brain sends `CMD_EXECUTE`, the child triggers a local "running" visual via `tft_ui_trigger_execute()` plus a short matrix animation.
- Reset behavior: `CMD_RESET` returns the TFT to idle, clears the local matrix, and clears the shared status strip.

For task-6 LED mirroring, these same blocks also accept Brain-driven `CMD_MATRIX_FILL`, `CMD_MATRIX_BRIGHTNESS`, `CMD_MATRIX_SHOW`, and `CMD_MATRIX_CLEAR` commands through the shared `status_strip` component. That keeps idle type-color mapping and executor highlighting synchronized across:

- the Brain's local strip program map
- child status strips
- the control-flow block TFT/matrix local execute feedback

With issue `#67`, canonical blocks additionally accept `CMD_RUNTIME_BROADCAST` and render shared parity UX from the same command path:
- `IDLE`: identity-color idle visual, no audio
- `RUNNING`: running visual for the active program step
- `STEP`: highlighted current action plus a short tick for speaker-capable blocks
- `DONE`: success visual plus completion tone
- `ERROR` / `STOP`: error visual plus error/stop tone

### Block -> Brain control-flow config submit (I2C `STATUS_DATA_READY` + `CMD_GET_DATA`)

Some child blocks publish configuration or input events to the Brain via:

- Child asserts `STATUS_DATA_READY` in `REG_STATUS`
- Brain polls `REG_STATUS`, then issues `CMD_GET_DATA`
- Child returns a fixed-length payload (by block type)

The Brain interprets the first byte as a **block-originated event ID**, followed by an event-specific payload:

| Event ID | Name | Payload bytes | Layout |
|---:|---|---:|---|
| `0x01` | `BRAIN_BLOCK_EVENT_SELECTION_SUBMIT` | 1 | `selection` (uint8) |
| `0x02` | `BRAIN_BLOCK_EVENT_LOOP_COUNT_SUBMIT` | 1 | `loop_count` (uint8) |
| `0x03` | `BRAIN_BLOCK_EVENT_DELAY_MS_SUBMIT` | 4 | `delay_ms` (uint32 LE) |
| `0x04` | `BRAIN_BLOCK_EVENT_BUTTON_PRESS` | 1 | `pressed` (uint8, nonzero=true) |

**Per-program-position semantics**:
- LOOP: `loop_count` is stored per scanned program index (`pc`) based on the current `block_config_manager_get_state_snapshot()` ordering.
- DELAY: `delay_ms` is stored per scanned program index (`pc`) based on the current `block_config_manager_get_state_snapshot()` ordering.
- LED_FLASH / NOTE: selection submits are stored per program index (and stashed by I2C address until the next `START` binds them). During execution, LED uses the per-`pc` color when set; NOTE uses per-`pc` note or sequence—**single submit** plays one note, **sequence submit** plays every note in order before the executor advances past that NOTE step.
- During execution, when the executor hits a `BLOCK_TYPE_LOOP` or `BLOCK_TYPE_DELAY` at a given `pc`, it prefers the stored per-`pc` value; otherwise it falls back to `brain_executor_params_t.loop_count` / `brain_executor_params_t.delay_ms`.

### Scan-Derived Event Map (`block_config_manager`)

The Brain computes a `block_event_map_t` during each I2C scan (`block_config_manager_scan()`), to summarize IF/LOOP boundaries and basic “sequence body” metadata:

- `if_start_count`, `if_end_count`, `loop_start_count`, `loop_end_count`: counts of boundaries found in the scanned topology
- `sequence_count` + `sequences[]`: an array of `block_sequence_metadata_t` frames for each encountered IF/LOOP start, with:
  - `sequence_type`: IF or LOOP
  - `start_index`, `end_index`: indices in the scanned block list
  - `has_end_boundary`: whether a matching end boundary was detected
  - `has_input` / `has_output_or_delay` + counts: whether there are input blocks and runnable steps within the sequence body

Today, the executor primarily uses the program array and runtime scans to find matching boundaries; the event map is available for validation and richer execution policies.

## Block Configuration Manager

### `block_config_manager_init()`

Initialize the block configuration manager.

**Signature**:
```c
esp_err_t block_config_manager_init(void);
```

**Returns**:
- `ESP_OK`: Success
- Error code on failure

**Example**:
```c
esp_err_t ret = block_config_manager_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize config manager");
}
```

### `block_config_manager_update()`

Update configuration from device registry.

**Signature**:
```c
esp_err_t block_config_manager_update(void);
```

**Returns**:
- `ESP_OK`: Success
- Error code on failure

**Example**:
```c
esp_err_t ret = block_config_manager_update();
if (ret == ESP_OK && block_config_manager_has_changed()) {
    // Configuration changed, send update
}
```

### `block_config_manager_has_changed()`

Check if configuration changed since last update.

**Signature**:
```c
bool block_config_manager_has_changed(void);
```

**Returns**:
- `true`: Configuration changed
- `false`: No changes

### `block_config_manager_generate_json()`

Generate JSON representation of current configuration.

**Signature**:
```c
char* block_config_manager_generate_json(void);
```

**Returns**:
- Pointer to JSON string (caller must free)
- `NULL` on error

**Example**:
```c
char* json = block_config_manager_generate_json();
if (json != NULL) {
    // Send json via TCP
    free(json);
}
```

## I2C Communication

### `i2c_master_init()`

Initialize I2C master interface.

**Signature**:
```c
esp_err_t i2c_master_init(void);
```

**Returns**:
- `ESP_OK`: Success
- Error code on failure

**Configuration**:
- SDA: GPIO 21
- SCL: GPIO 22
- Speed: 100 kHz

### `i2c_safe_scan()`

Scan I2C bus for connected devices.

**Signature**:
```c
void i2c_safe_scan(void);
```

**Behavior**:
- Scans addresses 0x08-0x15
- Logs found devices
- Updates device registry

### `i2c_read_whoami()`

Read WHOAMI register from a device.

**Signature**:
```c
esp_err_t i2c_read_whoami(uint8_t addr, whoami_data_t* whoami);
```

**Parameters**:
- `addr`: I2C address (0x08-0x15)
- `whoami`: Pointer to store WHOAMI data

**Returns**:
- `ESP_OK`: Success
- Error code on failure

## TCP Client

### `wifi_init()`

Initialize Wi-Fi connection.

**Signature**:
```c
void wifi_init(void);
```

**Configuration**:
Set in `menuconfig` or `sdkconfig`:
- SSID
- Password

### `tcp_client_task()`

TCP client task (FreeRTOS task).

**Signature**:
```c
void tcp_client_task(void* pvParameters);
```

**Behavior**:
- Connects to Flutter app TCP server
- Sends configuration updates
- Handles reconnection

**Configuration**:
- Server IP: Set in code
- Server Port: 41233 (default)

## Data Structures

### `whoami_data_t`

Block identification data.

```c
typedef struct {
    char block_id[32];
    char block_type[32];
    uint8_t version;
} whoami_data_t;
```

### `block_info_t`

Block information structure.

```c
typedef struct {
    uint8_t index;
    uint8_t i2c_address;
    whoami_data_t whoami;
    uint8_t connection_order;
} block_info_t;
```

### `block_config_t`

Block configuration structure.

```c
typedef struct {
    uint8_t total_blocks;
    block_info_t blocks[MAX_BLOCKS];
    uint8_t error_count;
    config_error_t errors[MAX_ERRORS];
} block_config_t;
```

## Error Codes

Common ESP-IDF error codes:

- `ESP_OK`: Success (0)
- `ESP_ERR_INVALID_ARG`: Invalid argument
- `ESP_ERR_INVALID_STATE`: Invalid state
- `ESP_ERR_NO_MEM`: Out of memory
- `ESP_FAIL`: Generic failure

## Logging

Use ESP-IDF logging macros:

```c
ESP_LOGI(TAG, "Info message");
ESP_LOGW(TAG, "Warning message");
ESP_LOGE(TAG, "Error message");
ESP_LOGD(TAG, "Debug message");
ESP_LOGV(TAG, "Verbose message");
```

## Examples

### Complete Initialization

```c
// Initialize I2C
esp_err_t ret = i2c_master_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C init failed");
    return;
}

// Initialize config manager
ret = block_config_manager_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Config manager init failed");
    return;
}

// Create TCP client task
xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);
```

### Sending Configuration Update

```c
// Update configuration
block_config_manager_update();

// Check if changed
if (block_config_manager_has_changed()) {
    // Generate JSON
    char* json = block_config_manager_generate_json();
    if (json != NULL) {
        // Send via TCP
        send_config(json);
        free(json);
    }
}
```

## Resources

- [ESP-IDF API Reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/)
- [I2C Driver API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2c.html)
- [TCP/IP API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/tcp.html)
