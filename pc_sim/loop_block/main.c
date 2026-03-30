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

void loop_block_set_loop_count_from_ui(uint8_t loop_count)
{
    printf("[sim] LOOP submit count=%u\n", (unsigned)loop_count);
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
