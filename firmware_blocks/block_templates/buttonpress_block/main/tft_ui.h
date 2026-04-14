#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void tft_ui_start(void);
void tft_ui_trigger_execute(void);
void tft_ui_set_idle(void);
void tft_ui_set_press_now_visible(bool visible);

#ifdef __cplusplus
}
#endif
