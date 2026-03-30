#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "lvgl.h"
#include "src/drivers/sdl/lv_sdl_keyboard.h"
#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_mousewheel.h"
#include "src/drivers/sdl/lv_sdl_window.h"

#include "tft_ui.h"

static const char *const k_song_names[] = {
    "Espresso",
    "Birds of a Feather",
    "Beat It",
    "Hakuna Matata",
    "Baby Shark",
};

static const char *const k_song_files[] = {
    MUSIC_SEQ_SIM_AUDIO_DIR "/espresso.wav",
    MUSIC_SEQ_SIM_AUDIO_DIR "/birds_of_a_feather.wav",
    MUSIC_SEQ_SIM_AUDIO_DIR "/beat_it.wav",
    MUSIC_SEQ_SIM_AUDIO_DIR "/hakuna_matata.wav",
    MUSIC_SEQ_SIM_AUDIO_DIR "/babyshark.wav",
};

static const music_age_range_t k_song_ages[] = {
    MUSIC_AGE_RANGE_8_PLUS,
    MUSIC_AGE_RANGE_8_PLUS,
    MUSIC_AGE_RANGE_8_PLUS,
    MUSIC_AGE_RANGE_5_TO_7,
    MUSIC_AGE_RANGE_2_TO_4,
};

static const char *const k_age_labels[] = {
    "All Ages",
    "Ages 2-4",
    "Ages 5-7",
    "Ages 8+",
};

static SDL_AudioDeviceID s_audio_device = 0;
static Uint8 *s_audio_buffer = NULL;
static Uint32 s_audio_length = 0;
static size_t s_active_song_index = 0;
static bool s_audio_playing = false;

static void sim_audio_stop(void)
{
    if (s_audio_device != 0U) {
        SDL_ClearQueuedAudio(s_audio_device);
        SDL_CloseAudioDevice(s_audio_device);
        s_audio_device = 0;
    }

    if (s_audio_buffer != NULL) {
        SDL_FreeWAV(s_audio_buffer);
        s_audio_buffer = NULL;
    }

    s_audio_length = 0;
    s_audio_playing = false;
}

static bool sim_audio_init(void)
{
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0U) {
        return true;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "[sim] audio init failed: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

static void sim_audio_poll(void)
{
    if (!s_audio_playing || s_audio_device == 0U) {
        return;
    }

    if (SDL_GetQueuedAudioSize(s_audio_device) != 0U) {
        return;
    }

    sim_audio_stop();
    tft_ui_set_playback_state(&(music_playback_state_t) {
        .is_playing = false,
        .active_song_index = (uint8_t)s_active_song_index,
    });
    tft_ui_set_status_message("Done! Tap Select to confirm.");
}

size_t speaker_get_song_count(void)
{
    return sizeof(k_song_names) / sizeof(k_song_names[0]);
}

const char *speaker_get_song_name(size_t index)
{
    if (index >= speaker_get_song_count()) {
        return "Unknown Song";
    }
    return k_song_names[index];
}

music_age_range_t speaker_get_song_age_range(size_t index)
{
    if (index >= speaker_get_song_count()) {
        return MUSIC_AGE_RANGE_ALL;
    }
    return k_song_ages[index];
}

const char *speaker_get_age_range_label(music_age_range_t age_range)
{
    if ((size_t)age_range >= (sizeof(k_age_labels) / sizeof(k_age_labels[0]))) {
        return "Ages ?";
    }
    return k_age_labels[age_range];
}

esp_err_t speaker_play_song(size_t index)
{
    SDL_AudioSpec wav_spec;
    Uint8 *wav_buffer = NULL;
    Uint32 wav_length = 0;
    SDL_AudioDeviceID audio_device = 0;

    if (index >= speaker_get_song_count()) {
        return ESP_FAIL;
    }

    if (!sim_audio_init()) {
        return ESP_FAIL;
    }

    if (s_audio_playing) {
        sim_audio_stop();
    }

    if (SDL_LoadWAV(k_song_files[index], &wav_spec, &wav_buffer, &wav_length) == NULL) {
        fprintf(stderr, "[sim] failed to load %s: %s\n", k_song_files[index], SDL_GetError());
        return ESP_FAIL;
    }

    audio_device = SDL_OpenAudioDevice(NULL, 0, &wav_spec, NULL, 0);
    if (audio_device == 0U) {
        fprintf(stderr, "[sim] failed to open audio device: %s\n", SDL_GetError());
        SDL_FreeWAV(wav_buffer);
        return ESP_FAIL;
    }

    if (SDL_QueueAudio(audio_device, wav_buffer, wav_length) != 0) {
        fprintf(stderr, "[sim] failed to queue audio: %s\n", SDL_GetError());
        SDL_CloseAudioDevice(audio_device);
        SDL_FreeWAV(wav_buffer);
        return ESP_FAIL;
    }

    s_audio_device = audio_device;
    s_audio_buffer = wav_buffer;
    s_audio_length = wav_length;
    s_active_song_index = index;
    s_audio_playing = true;
    SDL_PauseAudioDevice(s_audio_device, 0);

    printf("[sim] preview song %zu: %s (%s) from %s\n",
           index,
           speaker_get_song_name(index),
           speaker_get_age_range_label(speaker_get_song_age_range(index)),
           k_song_files[index]);
    return ESP_OK;
}

int main(void)
{
    lv_init();

    (void)lv_sdl_window_create(240, 320);
    lv_sdl_mouse_create();
    (void)lv_sdl_mousewheel_create();
    (void)lv_sdl_keyboard_create();

    (void)tft_ui_start();

    while (1) {
        lv_timer_handler();
        sim_audio_poll();
        SDL_Delay(5);
        lv_tick_inc(5);
    }
}
