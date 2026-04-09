#include "music_leds.h"

#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "led_contract.h"
#include "led_matrix.h"

#define MUSIC_LED_COUNT 16U

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

typedef struct {
    uint16_t bpm;
    uint8_t beats_per_bar;
    uint8_t accent_period_beats;
    uint8_t steps_per_beat;
    uint8_t accent_step_offset;
} song_timing_t;

static const char *TAG = "MUSIC_LEDS";

static volatile bool s_pattern_active = false;
static volatile uint8_t s_active_song_id = 0U;
static bool s_leds_ready = false;
static TaskHandle_t s_pattern_task = NULL;
static uint32_t s_song_frame = 0U;
static uint32_t s_song_step = 0U;

static const rgb_t k_note_colors[7] = {
    {255, 32, 32},   /* A */
    {255, 128, 0},   /* B */
    {255, 220, 0},   /* C */
    {32, 200, 64},   /* D */
    {0, 170, 255},   /* E */
    {80, 96, 255},   /* F */
    {200, 64, 255},  /* G */
};

static const song_timing_t k_song_timing[] = {
    {104U, 4U, 4U, 1U, 0U},
    {105U, 4U, 2U, 1U, 0U},
    {89U,  4U, 1U, 2U, 1U},
    {115U, 4U, 4U, 1U, 0U},
};

static void music_leds_clear_and_show(void)
{
    matrix_clear();
    matrix_show();
}

static void music_leds_fill_and_show(rgb_t color)
{
    matrix_fill(color.r, color.g, color.b);
    matrix_show();
}

static uint32_t music_leds_get_beat_ms(uint8_t song_id)
{
    const size_t count = sizeof(k_song_timing) / sizeof(k_song_timing[0]);
    const song_timing_t *timing = &k_song_timing[song_id % count];
    uint8_t steps_per_beat = (timing->steps_per_beat == 0U) ? 1U : timing->steps_per_beat;

    if (timing->bpm == 0U) {
        return 500U;
    }

    return 60000U / (timing->bpm * steps_per_beat);
}

static void music_leds_render_song_frame(uint8_t song_id, uint32_t frame, uint32_t step)
{
    static const rgb_t k_song_colors[][3] = {
        { {255, 60, 180}, {64, 220, 255}, {255, 255, 255} },
        { {80, 200, 255}, {40, 80, 255}, {180, 255, 255} },
        { {255, 48, 32}, {255, 120, 0}, {180, 48, 0} },
        { {0, 120, 255}, {0, 220, 255}, {255, 255, 255} },
    };

    const size_t pattern_count = sizeof(k_song_colors) / sizeof(k_song_colors[0]);
    const size_t timing_count = sizeof(k_song_timing) / sizeof(k_song_timing[0]);
    const rgb_t *palette = k_song_colors[song_id % pattern_count];
    const song_timing_t *timing = &k_song_timing[song_id % timing_count];
    uint32_t i;
    uint8_t accent_period = (timing->accent_period_beats == 0U) ? timing->beats_per_bar : timing->accent_period_beats;
    uint8_t steps_per_beat = (timing->steps_per_beat == 0U) ? 1U : timing->steps_per_beat;
    uint32_t accent_period_steps = accent_period * steps_per_beat;
    uint32_t accent_offset = timing->accent_step_offset % steps_per_beat;
    bool downbeat = ((step % accent_period_steps) == accent_offset);

    matrix_clear();

    switch (song_id % pattern_count) {
        case 0:
            for (i = 0; i < MUSIC_LED_COUNT; i++) {
                matrix_set_pixel((uint16_t)i,
                                 ((i + frame) % 4U == 0U) ? palette[1].r : palette[0].r,
                                 ((i + frame) % 4U == 0U) ? palette[1].g : palette[0].g,
                                 ((i + frame) % 4U == 0U) ? palette[1].b : palette[0].b);
            }
            break;

        case 1:
            for (i = 0; i < MUSIC_LED_COUNT; i++) {
                rgb_t color = (((i / 2U) + frame) % 2U == 0U) ? (downbeat ? palette[2] : palette[0]) : palette[1];
                matrix_set_pixel((uint16_t)i, color.r, color.g, color.b);
            }
            break;

        case 2:
            for (i = 0; i < MUSIC_LED_COUNT; i++) {
                rgb_t color = (downbeat || i == (frame % MUSIC_LED_COUNT)) ? palette[2] : palette[0];
                matrix_set_pixel((uint16_t)i, color.r, color.g, color.b);
            }
            break;

        case 3:
            for (i = 0; i < MUSIC_LED_COUNT; i++) {
                uint32_t distance = (i > (frame % MUSIC_LED_COUNT)) ? (i - (frame % MUSIC_LED_COUNT)) : ((frame % MUSIC_LED_COUNT) - i);
                rgb_t color = downbeat ? palette[2] : ((distance <= 1U) ? palette[0] : ((distance <= 3U) ? palette[1] : palette[2]));
                matrix_set_pixel((uint16_t)i, color.r, color.g, color.b);
            }
            break;

        case 4:
        default:
            for (i = 0; i < MUSIC_LED_COUNT; i++) {
                bool crest = ((i + frame) % 4U) == 0U;
                rgb_t color = downbeat ? palette[2] : (crest ? palette[1] : palette[0]);
                matrix_set_pixel((uint16_t)i, color.r, color.g, color.b);
            }
            break;
    }

    matrix_show();
}

static void music_leds_pattern_task(void *arg)
{
    (void)arg;
    while (1) {
        if (s_pattern_active) {
            music_leds_render_song_frame(s_active_song_id, s_song_frame++, s_song_step++);
            vTaskDelay(pdMS_TO_TICKS(music_leds_get_beat_ms(s_active_song_id)));
        } else {
            vTaskDelay(pdMS_TO_TICKS(40));
        }
    }
}

esp_err_t music_leds_init(void)
{
    esp_err_t err;

    if (s_leds_ready) {
        return ESP_OK;
    }

    err = led_matrix_init();
    if (err != ESP_OK) {
        return err;
    }

    matrix_set_brightness(80);
    s_leds_ready = true;

    if (s_pattern_task == NULL) {
        BaseType_t ok = xTaskCreatePinnedToCore(music_leds_pattern_task,
                                                "music_leds",
                                                4096,
                                                NULL,
                                                3,
                                                &s_pattern_task,
                                                0);
        if (ok != pdPASS) {
            s_pattern_task = NULL;
            s_leds_ready = false;
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

void music_leds_show_startup(void)
{
    if (!s_leds_ready) {
        return;
    }

    music_leds_clear_and_show();
}

void music_leds_show_idle(void)
{
    led_contract_rgb_t identity;
    led_contract_rgb_t color;

    if (!s_leds_ready) {
        return;
    }

    identity = led_contract_identity_color(BLOCK_TYPE_MUSIC_SEQ);
    color = led_contract_status_color(STATUS_READY, identity);
    music_leds_fill_and_show((rgb_t){color.r, color.g, color.b});
}

void music_leds_show_note_color(uint8_t note_id, uint32_t hold_ms)
{
    uint32_t index = (note_id < 7U) ? note_id : 0U;

    if (!s_leds_ready) {
        return;
    }

    s_pattern_active = false;
    music_leds_fill_and_show(k_note_colors[index]);
    if (hold_ms > 0U) {
        vTaskDelay(pdMS_TO_TICKS(hold_ms));
        music_leds_show_idle();
    }
}

void music_leds_start_song_pattern(uint8_t song_id)
{
    if (!s_leds_ready) {
        return;
    }

    s_active_song_id = song_id;
    s_song_frame = 0U;
    s_song_step = 0U;
    s_pattern_active = true;
    ESP_LOGI(TAG, "Started song pattern %u", (unsigned)song_id);
}

void music_leds_stop_song_pattern(void)
{
    if (!s_leds_ready) {
        return;
    }

    s_pattern_active = false;
    music_leds_show_idle();
}
