#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_keyboard.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_mousewheel.h"
#include "src/drivers/sdl/lv_sdl_window.h"

#include "tft_ui.h"

void delay_block_set_delay_ms_from_ui(uint32_t delay_ms)
{
    printf("[sim] DELAY submit value=%lu ms\n", (unsigned long)delay_ms);
}

int main(void)
{
    lv_init();

    (void)lv_sdl_window_create(240, 320);
    lv_sdl_mouse_create();
    (void)lv_sdl_mousewheel_create();
    (void)lv_sdl_keyboard_create();

    tft_ui_start();

    while (true) {
        lv_timer_handler();
        SDL_Delay(5);
        lv_tick_inc(5);
    }
}
