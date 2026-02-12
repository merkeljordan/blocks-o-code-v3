# Firmware API Reference

This document provides API reference for the ESP32 Brain Block firmware.

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
