#pragma once

/*
 * C-callable audio API I expose to the rest of the firmware.
 *
 * I keep this header simple on purpose so C files can call audio features
 * without touching the C++ classes directly.
 */

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called by: app startup (main.c)
 * Initializes DACOutput and starts writer task on silence source.
 */
esp_err_t speaker_init(void);

/* Called by: optional shutdown paths (lightweight in current implementation). */
void speaker_deinit(void);

/* Called by: UI/logic layer wanting a volume API.
 * Applies linear gain scaling to WAV and tone playback paths.
 */
void speaker_set_volume(uint8_t volume_percent);
uint8_t speaker_get_volume(void);

/* Called by: stop/reset paths. Switches active source to silence. */
esp_err_t speaker_stop(void);

/* Called by: main.c / speaker_music.c */
esp_err_t speaker_play_boot_sound(void);
esp_err_t speaker_play_wav(const uint8_t *data, size_t len);
esp_err_t speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms);

/* Called by: startup/error feedback paths. */
void speaker_beep_ok(void);
void speaker_beep_error(void);

#ifdef __cplusplus
}
#endif
