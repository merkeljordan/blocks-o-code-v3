# Firmware API Reference

This document provides API reference for the ESP32 Brain Block firmware.

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
```

Executor state enum:
- `EXECUTOR_IDLE`
- `EXECUTOR_RUNNING`
- `EXECUTOR_WAIT_INPUT`
- `EXECUTOR_WAIT_DELAY`
- `EXECUTOR_STOPPED`
- `EXECUTOR_DONE`

Notes:
- Executor is tick-driven (periodic task) and non-blocking.
- IF/LOOP/DELAY control flow is interpreted with a program counter.
- Config refresh during execution triggers an execution stop to avoid stale topology behavior.

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
