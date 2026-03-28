#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "i2c_protocol.h"

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_contract_rgb_t;

typedef struct {
    bool supports_identity_color;
    bool supports_status_strip;
    bool supports_matrix;
    bool supports_pattern_playback;
    bool mirror_matrix_to_strip;
} led_contract_caps_t;

static inline led_contract_rgb_t led_contract_identity_color(block_type_t type)
{
    switch (type) {
        case BLOCK_TYPE_BRAIN:
            return (led_contract_rgb_t){255U, 0U, 0U};
        case BLOCK_TYPE_IF:
        case BLOCK_TYPE_THEN:
        case BLOCK_TYPE_END_IF:
            return (led_contract_rgb_t){0U, 180U, 60U};
        case BLOCK_TYPE_LOOP:
        case BLOCK_TYPE_END_LOOP:
            return (led_contract_rgb_t){0U, 40U, 255U};
        case BLOCK_TYPE_DELAY:
            return (led_contract_rgb_t){255U, 60U, 0U};
        case BLOCK_TYPE_BUTTON:
            return (led_contract_rgb_t){255U, 0U, 255U};
        case BLOCK_TYPE_NOTE:
            return (led_contract_rgb_t){255U, 220U, 0U};
        case BLOCK_TYPE_MUSIC_SEQ:
            return (led_contract_rgb_t){0U, 210U, 170U};
        case BLOCK_TYPE_LED_FLASH:
            return (led_contract_rgb_t){180U, 70U, 255U};
        case BLOCK_TYPE_DISCO:
            return (led_contract_rgb_t){255U, 255U, 255U};
        default:
            return (led_contract_rgb_t){32U, 32U, 32U};
    }
}

static inline bool led_contract_supports_brain_mirroring(block_type_t type)
{
    switch (type) {
        case BLOCK_TYPE_IF:
        case BLOCK_TYPE_THEN:
        case BLOCK_TYPE_END_IF:
        case BLOCK_TYPE_LOOP:
        case BLOCK_TYPE_END_LOOP:
        case BLOCK_TYPE_DELAY:
        case BLOCK_TYPE_BUTTON:
        case BLOCK_TYPE_NOTE:
        case BLOCK_TYPE_MUSIC_SEQ:
        case BLOCK_TYPE_LED_FLASH:
            return true;
        default:
            return false;
    }
}

static inline led_contract_caps_t led_contract_get_caps(block_type_t type)
{
    switch (type) {
        case BLOCK_TYPE_IF:
        case BLOCK_TYPE_THEN:
        case BLOCK_TYPE_END_IF:
        case BLOCK_TYPE_LOOP:
        case BLOCK_TYPE_END_LOOP:
        case BLOCK_TYPE_DELAY:
        case BLOCK_TYPE_BUTTON:
        case BLOCK_TYPE_NOTE:
            return (led_contract_caps_t){
                .supports_identity_color = true,
                .supports_status_strip = true,
                .supports_matrix = true,
                .supports_pattern_playback = true,
                .mirror_matrix_to_strip = true,
            };
        case BLOCK_TYPE_LED_FLASH:
            return (led_contract_caps_t){
                .supports_identity_color = true,
                .supports_status_strip = true,
                .supports_matrix = true,
                .supports_pattern_playback = true,
                .mirror_matrix_to_strip = true,
            };
        case BLOCK_TYPE_MUSIC_SEQ:
            return (led_contract_caps_t){
                .supports_identity_color = true,
                .supports_status_strip = true,
                .supports_matrix = true,
                .supports_pattern_playback = true,
                .mirror_matrix_to_strip = true,
            };
        default:
            return (led_contract_caps_t){
                .supports_identity_color = false,
                .supports_status_strip = false,
                .supports_matrix = false,
                .supports_pattern_playback = false,
                .mirror_matrix_to_strip = false,
            };
    }
}

static inline led_contract_rgb_t led_contract_status_color(uint8_t status, led_contract_rgb_t identity)
{
    if ((status & STATUS_ERROR) != 0U) {
        return (led_contract_rgb_t){255U, 0U, 0U};
    }
    if ((status & STATUS_BUSY) != 0U) {
        return identity;
    }
    if ((status & STATUS_DATA_READY) != 0U) {
        return identity;
    }
    return identity;
}

static inline uint8_t led_contract_status_brightness(uint8_t status)
{
    if ((status & STATUS_ERROR) != 0U) {
        return 160U;
    }
    if ((status & STATUS_BUSY) != 0U) {
        return 255U;
    }
    return 96U;
}
