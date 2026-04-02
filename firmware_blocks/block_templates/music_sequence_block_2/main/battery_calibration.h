#pragma once

/**
 * @file battery_calibration.h
 * @brief Per-block battery ADC calibration constants for music_sequence_block_2.
 *
 * These calibration values are specific to the music_sequence_block_2 PCB and account for
 * resistor tolerances, ADC gain variations, and PCB-specific voltage divider
 * characteristics. Update these values after physical battery voltage measurements.
 *
 * Calibration process:
 * 1. Measure actual battery voltage with a multimeter (when possible)
 * 2. Observe ADC readings from serial logs
 * 3. Calculate correction: BATTERY_CAL_SCALE = actual_mv / adc_reading_mv
 * 4. Update BATTERY_CAL_SCALE and optionally BATTERY_CAL_OFFSET_MV below
 * 5. Rebuild and flash firmware
 * 6. Commit changes to git with calibration date/notes
 */

/**
 * Multiplicative correction for ADC/resistor divider tolerances.
 * Formula: corrected_voltage_mv = (raw_adc_mv * BATTERY_CAL_SCALE) + BATTERY_CAL_OFFSET_MV
 *
 * Default: 0.995f (typical ~0.5% tolerance on resistor divider)
 * Adjust if measurements show systematic over/underestimation.
 */
#define BATTERY_CAL_SCALE 0.990f

/**
 * Additive correction for fixed voltage offset (e.g., diode drop, op-amp bias).
 * Units: millivolts
 *
 * Default: 0.0f
 * Adjust if readings still miss target after BATTERY_CAL_SCALE tuning.
 */
#define BATTERY_CAL_OFFSET_MV 0.0f
