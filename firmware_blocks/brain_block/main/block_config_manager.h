/*
 * block_config_manager.h
 *
 * Block Configuration Manager for Brain Block
 * Scans I2C-connected blocks, collects WHOAMI data, detects configuration changes,
 * and generates JSON representation for transmission to Flutter app.
 */

#ifndef BLOCK_CONFIG_MANAGER_H
#define BLOCK_CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "i2c_protocol.h"

// Maximum number of blocks that can be detected
#define BLOCK_CONFIG_MAX_BLOCKS 15

// Block information structure
typedef struct {
    uint8_t i2c_address;          // I2C address (0x08-0x16)
    block_type_t block_type;       // Block type from REG_WHOAMI
    uint8_t fw_major;              // Firmware major version (0 if unavailable)
    uint8_t fw_minor;              // Firmware minor version (0 if unavailable)
    uint8_t caps;                  // Capabilities byte (0 if unavailable)
    bool present;                  // True if block is currently responding
    int connection_order;         // Connection order (0, 1, 2, ...)
} block_config_entry_t;

// Configuration state structure
typedef struct {
    block_config_entry_t blocks[BLOCK_CONFIG_MAX_BLOCKS];
    uint8_t block_count;            // Number of detected blocks
    uint8_t error_count;            // Number of errors detected
    bool has_changed;               // True if configuration changed since last scan
    uint32_t scan_id;               // Monotonic scan counter for log correlation
    uint64_t last_scan_timestamp;   // Timestamp of last scan (milliseconds)
} block_config_state_t;

/**
 * @brief Initialize the block configuration manager
 */
void block_config_manager_init(void);

/**
 * @brief Scan I2C bus and update configuration state
 * @return ESP_OK on success
 */
esp_err_t block_config_manager_scan(void);

/**
 * @brief Check if configuration has changed since last scan
 * @return true if configuration changed, false otherwise
 */
bool block_config_manager_has_changed(void);

/**
 * @brief Get the number of errors in current configuration
 * @return Number of errors
 */
uint8_t block_config_manager_get_error_count(void);

/**
 * @brief Generate JSON string representation of current configuration
 * @param json_buffer Buffer to store JSON string (must be large enough, recommend 2048 bytes)
 * @param buffer_size Size of json_buffer
 * @return ESP_OK on success, ESP_ERR_INVALID_SIZE if buffer too small
 */
esp_err_t block_config_manager_get_json(char *json_buffer, size_t buffer_size);

/**
 * @brief Get pointer to current configuration state (read-only)
 * @return Pointer to configuration state
 */
const block_config_state_t* block_config_manager_get_state(void);

/**
 * @brief Copy current configuration state atomically into caller-provided storage
 * @param out_state Destination buffer
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out_state is NULL
 */
esp_err_t block_config_manager_get_state_snapshot(block_config_state_t *out_state);

/**
 * @brief Convert block_type_t to string identifier for JSON
 * @param type Block type enum value
 * @return String identifier (e.g., "if_block", "brain_block")
 */
const char* block_type_to_json_string(block_type_t type);

#endif // BLOCK_CONFIG_MANAGER_H
