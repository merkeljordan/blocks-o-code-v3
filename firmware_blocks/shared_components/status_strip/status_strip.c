#include "status_strip.h"

#include <stdlib.h>
#include <string.h>

#include "audio_speaker.h"
#include "esp_log.h"
#include "led_contract.h"
#include "led_matrix.h"
#include "led_strip.h"

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} status_strip_pixel_t;

static const char *TAG = "STATUS_STRIP";
static const uint8_t STATUS_STRIP_SAFE_BRIGHTNESS_MAX = 96U;
static led_strip_handle_t s_strip = NULL;
static status_strip_pixel_t *s_pixels = NULL;
static gpio_num_t s_gpio_num = GPIO_NUM_NC;
static uint16_t s_led_count = 0;
static uint8_t s_brightness = 96U;
static bool s_last_runtime_audio_valid = false;
static brain_runtime_broadcast_state_t s_last_runtime_audio_state = BRAIN_RUNTIME_IDLE;
static uint8_t s_last_runtime_audio_pc = BRAIN_RUNTIME_PC_NONE;
static uint8_t s_last_runtime_audio_step_type = BLOCK_TYPE_UNKNOWN;

static uint8_t scale_channel(uint8_t channel)
{
    return (uint8_t)(((uint16_t)channel * s_brightness) / 255U);
}

static bool status_strip_is_ready(void)
{
    return (s_strip != NULL && s_pixels != NULL && s_led_count > 0U);
}

led_contract_rgb_t status_strip_runtime_color(brain_runtime_broadcast_state_t state,
                                              block_type_t block_type,
                                              uint8_t step_type)
{
    led_contract_rgb_t identity = led_contract_identity_color(block_type);
    if (step_type != BLOCK_TYPE_UNKNOWN) {
        identity = led_contract_identity_color((block_type_t)step_type);
    }

    switch (state) {
        case BRAIN_RUNTIME_IDLE:
            return led_contract_identity_color(block_type);
        case BRAIN_RUNTIME_RUNNING:
        case BRAIN_RUNTIME_STEP:
            return identity;
        case BRAIN_RUNTIME_DONE:
            return (led_contract_rgb_t){0U, 255U, 0U};
        case BRAIN_RUNTIME_ERROR:
        case BRAIN_RUNTIME_STOP:
            return (led_contract_rgb_t){255U, 0U, 0U};
        default:
            return identity;
    }
}

uint8_t status_strip_runtime_brightness(brain_runtime_broadcast_state_t state)
{
    switch (state) {
        case BRAIN_RUNTIME_IDLE:
            return 64U;
        case BRAIN_RUNTIME_RUNNING:
            return 80U;
        case BRAIN_RUNTIME_STEP:
            return 96U;
        case BRAIN_RUNTIME_DONE:
            return 255U;
        case BRAIN_RUNTIME_ERROR:
        case BRAIN_RUNTIME_STOP:
            return 255U;
        default:
            return 64U;
    }
}

void status_strip_play_runtime_audio(brain_runtime_broadcast_state_t state)
{
    switch (state) {
        case BRAIN_RUNTIME_STEP:
            (void)speaker_play_tone(880U, 35U);
            break;
        case BRAIN_RUNTIME_DONE:
            speaker_beep_ok();
            break;
        case BRAIN_RUNTIME_ERROR:
        case BRAIN_RUNTIME_STOP:
            speaker_beep_error();
            break;
        default:
            break;
    }
}

void status_strip_play_runtime_audio_event(brain_runtime_broadcast_state_t state,
                                           uint8_t pc,
                                           uint8_t step_type)
{
    bool should_play = false;

    if (!s_last_runtime_audio_valid) {
        should_play = (state == BRAIN_RUNTIME_RUNNING ||
                       state == BRAIN_RUNTIME_STEP ||
                       state == BRAIN_RUNTIME_DONE ||
                       state == BRAIN_RUNTIME_ERROR ||
                       state == BRAIN_RUNTIME_STOP);
    } else {
        switch (state) {
            case BRAIN_RUNTIME_RUNNING:
                should_play = (s_last_runtime_audio_state == BRAIN_RUNTIME_IDLE ||
                               s_last_runtime_audio_state == BRAIN_RUNTIME_DONE ||
                               s_last_runtime_audio_state == BRAIN_RUNTIME_ERROR ||
                               s_last_runtime_audio_state == BRAIN_RUNTIME_STOP);
                break;
            case BRAIN_RUNTIME_STEP:
                should_play = (s_last_runtime_audio_state != BRAIN_RUNTIME_STEP ||
                               s_last_runtime_audio_pc != pc ||
                               s_last_runtime_audio_step_type != step_type);
                break;
            case BRAIN_RUNTIME_DONE:
            case BRAIN_RUNTIME_ERROR:
            case BRAIN_RUNTIME_STOP:
                should_play = (s_last_runtime_audio_state != state);
                break;
            case BRAIN_RUNTIME_IDLE:
            default:
                break;
        }
    }

    if (should_play) {
        if (state == BRAIN_RUNTIME_RUNNING) {
            (void)speaker_play_tone(660U, 45U);
        } else {
            status_strip_play_runtime_audio(state);
        }
    }

    s_last_runtime_audio_valid = true;
    s_last_runtime_audio_state = state;
    s_last_runtime_audio_pc = pc;
    s_last_runtime_audio_step_type = step_type;
}

esp_err_t status_strip_render_runtime_visuals(const char *tag,
                                              const status_strip_config_t *cfg,
                                              block_type_t block_type,
                                              brain_runtime_broadcast_state_t state,
                                              uint8_t pc,
                                              uint8_t step_type)
{
    led_contract_rgb_t color = status_strip_runtime_color(state, block_type, step_type);
    uint8_t brightness = status_strip_runtime_brightness(state);
    bool is_terminal = (state == BRAIN_RUNTIME_DONE ||
                        state == BRAIN_RUNTIME_ERROR ||
                        state == BRAIN_RUNTIME_STOP);

    led_matrix_set_lock(false); // Unlock to allow our own update
    matrix_set_brightness(brightness);
    matrix_fill(color.r, color.g, color.b);
    matrix_show();

    if (is_terminal) {
        led_matrix_set_lock(true); // Lock for terminal states
    }

    if (cfg != NULL && status_strip_ensure_ready(cfg) == ESP_OK) {
        status_strip_fill(color.r, color.g, color.b);
        status_strip_set_brightness(brightness);
        if (pc != BRAIN_RUNTIME_PC_NONE && status_strip_get_led_count() > 0U) {
            status_strip_set_pixel((uint16_t)(pc % status_strip_get_led_count()), 255U, 255U, 255U);
        }
        return status_strip_show();
    }

    (void)tag;
    return ESP_OK;
}

esp_err_t status_strip_ensure_ready(const status_strip_config_t *cfg)
{
    if (cfg == NULL || cfg->gpio_num == GPIO_NUM_NC || cfg->led_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if (status_strip_is_ready()) {
        if (s_gpio_num == cfg->gpio_num && s_led_count == cfg->led_count) {
            return ESP_OK;
        }
        ESP_LOGE(TAG,
                 "status strip already initialized on GPIO %d (%u LEDs), requested GPIO %d (%u LEDs)",
                 (int)s_gpio_num,
                 (unsigned)s_led_count,
                 (int)cfg->gpio_num,
                 (unsigned)cfg->led_count);
        return ESP_ERR_INVALID_STATE;
    }

    s_pixels = calloc(cfg->led_count, sizeof(*s_pixels));
    if (s_pixels == NULL) {
        ESP_LOGE(TAG, "Failed to allocate pixel buffer for %u LEDs", (unsigned)cfg->led_count);
        return ESP_ERR_NO_MEM;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = cfg->gpio_num,
        .max_leds = cfg->led_count,
#ifdef LED_STRIP_COLOR_COMPONENT_FMT_GRB
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
#else
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
#endif
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create status strip: %s", esp_err_to_name(err));
        free(s_pixels);
        s_pixels = NULL;
        return err;
    }

    s_gpio_num = cfg->gpio_num;
    s_led_count = cfg->led_count;
    s_brightness = 96U;

    err = led_strip_clear(s_strip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Initial clear failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Initialized status strip on GPIO %d with %u LEDs",
             (int)s_gpio_num, (unsigned)s_led_count);
    return ESP_OK;
}

void status_strip_fill(uint8_t r, uint8_t g, uint8_t b)
{
    if (!status_strip_is_ready()) {
        return;
    }

    for (uint16_t i = 0; i < s_led_count; i++) {
        s_pixels[i].r = r;
        s_pixels[i].g = g;
        s_pixels[i].b = b;
    }
}

void status_strip_set_pixel(uint16_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (!status_strip_is_ready() || idx >= s_led_count) {
        return;
    }

    s_pixels[idx].r = r;
    s_pixels[idx].g = g;
    s_pixels[idx].b = b;
}

void status_strip_clear(void)
{
    if (!status_strip_is_ready()) {
        return;
    }

    memset(s_pixels, 0, s_led_count * sizeof(*s_pixels));
}

void status_strip_set_brightness(uint8_t brightness)
{
    if (brightness > STATUS_STRIP_SAFE_BRIGHTNESS_MAX) {
        brightness = STATUS_STRIP_SAFE_BRIGHTNESS_MAX;
    }
    s_brightness = brightness;
}

uint8_t status_strip_get_brightness(void)
{
    return s_brightness;
}

uint16_t status_strip_get_led_count(void)
{
    return s_led_count;
}

esp_err_t status_strip_show(void)
{
    if (!status_strip_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint16_t i = 0; i < s_led_count; i++) {
        esp_err_t err = led_strip_set_pixel(s_strip,
                                            i,
                                            scale_channel(s_pixels[i].r),
                                            scale_channel(s_pixels[i].g),
                                            scale_channel(s_pixels[i].b));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set pixel %u: %s", (unsigned)i, esp_err_to_name(err));
            return err;
        }
    }

    return led_strip_refresh(s_strip);
}

esp_err_t status_strip_reset(const status_strip_config_t *cfg)
{
    esp_err_t err = status_strip_ensure_ready(cfg);
    if (err != ESP_OK) {
        return err;
    }

    status_strip_clear();
    return status_strip_show();
}

bool status_strip_handle_matrix_command(const char *tag,
                                        const status_strip_config_t *cfg,
                                        i2c_command_t cmd,
                                        const uint8_t *payload,
                                        size_t payload_len)
{
    if (cmd != CMD_MATRIX_FILL &&
        cmd != CMD_MATRIX_CLEAR &&
        cmd != CMD_MATRIX_BRIGHTNESS &&
        cmd != CMD_MATRIX_SHOW) {
        return false;
    }

    esp_err_t err = status_strip_ensure_ready(cfg);
    if (err != ESP_OK) {
        ESP_LOGE(tag ? tag : TAG, "Status strip init failed: %s", esp_err_to_name(err));
        return true;
    }

    switch (cmd) {
        case CMD_MATRIX_FILL:
            if (payload_len >= 3U) {
                status_strip_fill(payload[0], payload[1], payload[2]);
                ESP_LOGI(tag ? tag : TAG, "Status strip fill RGB(%u, %u, %u)",
                         (unsigned)payload[0], (unsigned)payload[1], (unsigned)payload[2]);
            }
            return true;

        case CMD_MATRIX_CLEAR:
            status_strip_clear();
            ESP_LOGI(tag ? tag : TAG, "Status strip clear");
            return true;

        case CMD_MATRIX_BRIGHTNESS:
            if (payload_len >= 1U) {
                status_strip_set_brightness(payload[0]);
                ESP_LOGI(tag ? tag : TAG, "Status strip brightness %u", (unsigned)payload[0]);
            }
            return true;

        case CMD_MATRIX_SHOW:
            err = status_strip_show();
            if (err != ESP_OK) {
                ESP_LOGE(tag ? tag : TAG, "Status strip show failed: %s", esp_err_to_name(err));
            }
            return true;

        default:
            return false;
    }
}

bool status_strip_handle_runtime_broadcast(const char *tag,
                                           const status_strip_config_t *cfg,
                                           block_type_t block_type,
                                           i2c_command_t cmd,
                                           const uint8_t *payload,
                                           size_t payload_len)
{
    if (cmd != CMD_RUNTIME_BROADCAST) {
        return false;
    }

    if (payload == NULL || payload_len < BRAIN_RUNTIME_BROADCAST_PAYLOAD_LEN) {
        ESP_LOGW(tag ? tag : TAG, "Runtime broadcast payload too short (%u)", (unsigned)payload_len);
        return true;
    }

    brain_runtime_broadcast_state_t state = (brain_runtime_broadcast_state_t)payload[0];
    uint8_t pc = payload[1];
    uint8_t step_type = payload[2];
    (void)status_strip_render_runtime_visuals(tag, cfg, block_type, state, pc, step_type);
    // Runtime beeps play only on the Brain (see broadcast_runtime_state); child blocks
    // only update LEDs so the bus does not trigger overlapping speaker tones.
    return true;
}
