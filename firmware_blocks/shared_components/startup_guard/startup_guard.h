#pragma once

/**
 * @brief Initialise GPIOs to safe states before any peripheral driver starts.
 *
 * Drives the amp-enable pin high (muting the amplifier) and holds the LED
 * data/enable pins low, then waits a short settle time.  Call this as the
 * very first thing in app_main() to prevent boot glitches from the audio
 * amplifier or LED strip drivers.
 */
void startup_power_guard(void);
