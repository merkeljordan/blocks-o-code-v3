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

// Address range for child blocks
#define DEVICE_REGISTRY_ADDR_MIN    0x08
#define DEVICE_REGISTRY_ADDR_MAX    0x33
#define DEVICE_REGISTRY_MAX_DEVICES (DEVICE_REGISTRY_ADDR_MAX - DEVICE_REGISTRY_ADDR_MIN + 1)

// Device entry in the registry
typedef struct {
    uint8_t address;        // I2C address
    block_type_t type;      // Block type from REG_WHOAMI
    bool present;           // True if device responded
} device_entry_t;

// Registry structure
typedef struct {
    device_entry_t devices[DEVICE_REGISTRY_MAX_DEVICES];
    uint8_t count;          // Number of devices found
} device_registry_t;

/**
 * @brief Initialize the device registry
 */
void device_registry_init(void);

/**
 * @brief Scan I2C bus for devices and read REG_WHOAMI from each
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