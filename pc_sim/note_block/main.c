#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SDL_MAIN_HANDLED /* Avoid SDL WinMain linker issues on Windows */
#include <SDL2/SDL.h>

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_keyboard.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_mousewheel.h"
#include "src/drivers/sdl/lv_sdl_window.h"

#include "tft_ui.h"

/* These are normally provided by the ESP-IDF Note Block firmware (main.c).
 * For the simulator we provide simple stubs so the UI can run standalone. */
void note_block_preview_note(uint8_t note_id)
{
    static const char *k_names[7] = {"A", "B", "C", "D", "E", "F", "G"};
    const char *name = (note_id < 7) ? k_names[note_id] : "?";
    printf("[sim] preview note %s (%u)\n", name, (unsigned)note_id);
}

bool note_block_submit_selection(uint8_t note_id)
{
    static const char *k_names[7] = {"A", "B", "C", "D", "E", "F", "G"};
    const char *name = (note_id < 7) ? k_names[note_id] : "?";
    printf("[sim] submit note %s (%u)\n", name, (unsigned)note_id);
    return true;
}

int main(void)
{
    lv_init();

    /* Match the physical TFT resolution so layouts are 1:1. */
    (void)lv_sdl_window_create(240, 320);

    lv_sdl_mouse_create();
    (void)lv_sdl_mousewheel_create();
    (void)lv_sdl_keyboard_create();

    tft_ui_start();

    while (1) {
        lv_timer_handler();
        SDL_Delay(5);
        lv_tick_inc(5);
    }
}

