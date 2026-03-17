/**
 * Minimal LVGL config for the Note Block PC simulator (SDL).
 *
 * This file is intentionally small; enable more features as needed.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* Needed so LVGL can include this file as "lv_conf.h" from the include path. */
#define LV_CONF_INCLUDE_SIMPLE 1

/* Color depth should match the embedded target (ILI9341 RGB565). */
#define LV_COLOR_DEPTH 16

/* Keep it simple for the simulator. */
#define LV_USE_OS LV_OS_NONE

/* Enable LVGL's built-in SDL window/input driver. */
#define LV_USE_SDL 1
#if LV_USE_SDL
    #define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
    #define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT
    #define LV_SDL_BUF_COUNT 1
    #define LV_SDL_ACCELERATED 1
    #define LV_SDL_FULLSCREEN 0
    #define LV_SDL_DIRECT_EXIT 1
    #define LV_SDL_MOUSEWHEEL_MODE LV_SDL_MOUSEWHEEL_MODE_ENCODER
#endif

/* Fonts used by your UI (keep Montserrat 14 enabled). */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Widgets used by the Note Block screens. */
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1

/* Layouts used by LVGL v9 defaults. */
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/* Disable demos/examples to keep build faster. */
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_RENDER 0
#define LV_USE_DEMO_STRESS 0
#define LV_USE_DEMO_MUSIC 0

#endif /* LV_CONF_H */

