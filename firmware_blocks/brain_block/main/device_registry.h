/*
 * device_registry.h
 *
 * Brain-side device registry for tracking connected I2C child blocks.
 */

#ifndef DEVICE_REGISTRY_H
#define DEVICE_REGISTRY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "i2c_protocol.h"

// Address range for child blocks (inclusive): 0x08–0x16 (15 addresses)
#define DEVICE_REGISTRY_ADDR_MIN       0x08
#define DEVICE_REGISTRY_ADDR_MAX       0x77
#define DEVICE_REGISTRY_MAX_DEVICES    15
#define DEVICE_REGISTRY_CONFIDENCE_MAX 10

// Device entry in the registry
typedef struct {
    uint8_t address;        // I2C address
    block_type_t type;      // Block type inferred from fixed child I2C address
    uint32_t uid;           // Stable per-device UID from REG_UID[0..3]
    bool present;           // True if device responded
    uint8_t confidence;     // Sticky detection confidence (0..10)
    uint8_t physical_position; // Stable active-chain position
} device_entry_t;

typedef struct {
    uint8_t address;
    uint8_t physical_position;
    bool occupied;
} active_chain_entry_t;

// Registry structure
typedef struct {
    device_entry_t devices[DEVICE_REGISTRY_MAX_DEVICES];
    active_chain_entry_t active_chain[DEVICE_REGISTRY_MAX_DEVICES];
    uint8_t count;          // Number of devices found
} device_registry_t;

/**
 * @brief Initialize the device registry
 */
void device_registry_init(void);

/**
 * @brief Scan I2C bus for devices; type is inferred from each child's fixed address
 * @return ESP_OK on success
 */
esp_err_t device_registry_scan(void);

/**
 * @brief Get pointer to the current registry (read-only)
 * @return Pointer to device registry
 */
const device_registry_t* device_registry_get(void);

/**
 * @brief Copy the current registry atomically into caller-provided storage
 * @param out_registry Destination buffer
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out_registry is NULL
 */
esp_err_t device_registry_get_snapshot(device_registry_t *out_registry);

/**
 * @brief Print the registry to the log
 */
void device_registry_print(void);

/**
 * @brief Get device entry by I2C address
 * @param address I2C address to look up
 * @return Pointer to device entry, or NULL if not found
 */
const device_entry_t* device_registry_find(uint8_t address);

/**
 * @brief Get number of detected devices
 * @return Count of present devices
 */
uint8_t device_registry_count(void);

#endif // DEVICE_REGISTRY_H
