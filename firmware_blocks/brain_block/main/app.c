/*
ESP32 brain_block - Wi‑Fi STA + TCP client that connects to a desktop TCP server
Bi-directional over LAN: ESP connects to a desktop server (always-listening), sends
and receives messages.

Notes:
- Requires ESP-IDF (tested against typical esp-idf TCP client examples).
- Edit WIFI_SSID, WIFI_PASS, SERVER_IP and SERVER_PORT below for your LAN.
- For production, move secrets to menuconfig or secure storage.
- This is a simple resilient example: it reconnects Wi‑Fi and TCP on failures.

Compile with the project's existing CMake and sdkconfig.

Behavior:
- Connects to configured Wi‑Fi SSID (station mode).
- When Wi‑Fi is connected, starts a TCP client task that connects to SERVER_IP:SERVER_PORT.
- Sends a test "hello" line when connected and echoes received data to serial.
- Reconnect logic with delays on failure.

Enhancement:
- Parse received newline-terminated commands ("START", "STOP") and
  instruct Child Block 1 via I2C using existing i2c helpers.
- Send simple acknowledgements back to the app.
*/

#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "brain_block.h" // exposes i2c helpers and device registry
#include "block_config_manager.h"
#include "brain_event_handler.h"
#include "cJSON.h"
#include "tft_ui.h"


#define WIFI_SSID       "Jordan" // <-- Set your Wi‑Fi SSID here
#define WIFI_PASS       "blocksocode"       // <-- Set your Wi‑Fi password here

/* Desktop server IP and port to connect to (set to your desktop listening server) */
#define SERVER_IP       "172.20.10.3" // <-- Set your server's IP address here (ipconfig)
#define SERVER_PORT     41233

/* reconnect / timing settings */
#define WIFI_RETRY_MS         5000
#define TCP_RETRY_MS          2000
#define TCP_SEND_INTERVAL_MS  5000
#define TCP_RX_BUF_SIZE       512
#define BLOCK_CONFIG_SCAN_INTERVAL_MS  700   /* Faster app updates while keeping some bus headroom */
#define BLOCK_CONFIG_JSON_BUFFER_SIZE  2048  // JSON buffer size
#define BLOCK_CONFIG_SCAN_TASK_STACK_SIZE 8192

static const char *TAG = "brain_block";
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
static bool s_validation_requested_by_start = false;
static volatile bool s_companion_connected = false;

// Latest block config JSON produced by scan task, sent by TCP task.
static EventGroupHandle_t s_block_config_event_group;
#define BLOCK_CONFIG_CHANGED_BIT BIT0
static SemaphoreHandle_t s_block_config_json_mutex;
static char s_block_config_json[BLOCK_CONFIG_JSON_BUFFER_SIZE];
/* Keep scan JSON buffer out of task stack to avoid overflow in block_cfg_scan. */
static char s_block_config_scan_json_buffer[BLOCK_CONFIG_JSON_BUFFER_SIZE];
static size_t s_block_config_json_len = 0;
static bool s_block_config_json_valid = false;

static bool brain_executor_scan_pause_active(void);
static bool copy_latest_block_config_json(char *out, size_t out_size, size_t *out_len) {
    if (out == NULL || out_len == NULL || out_size == 0) {
        return false;
    }
    if (s_block_config_json_mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_block_config_json_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return false;
    }

    bool ok = false;
    if (s_block_config_json_valid && s_block_config_json_len > 0 && s_block_config_json_len < out_size) {
        memcpy(out, s_block_config_json, s_block_config_json_len);
        out[s_block_config_json_len] = '\0';
        *out_len = s_block_config_json_len;
        ok = true;
    }

    xSemaphoreGive(s_block_config_json_mutex);
    return ok;
}

static const char *runtime_state_to_json_string(brain_runtime_broadcast_state_t state)
{
    switch (state) {
        case BRAIN_RUNTIME_IDLE:    return "idle";
        case BRAIN_RUNTIME_RUNNING: return "running";
        case BRAIN_RUNTIME_STEP:    return "step";
        case BRAIN_RUNTIME_DONE:    return "done";
        case BRAIN_RUNTIME_ERROR:   return "error";
        case BRAIN_RUNTIME_STOP:    return "stop";
        default:                    return "unknown";
    }
}

static bool build_runtime_update_json(char *out, size_t out_size, size_t *out_len)
{
    if (out == NULL || out_len == NULL || out_size < 64u) {
        return false;
    }

    const brain_runtime_snapshot_t *runtime = brain_event_handler_get_runtime_snapshot();
    if (runtime == NULL) {
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return false;
    }

    cJSON_AddStringToObject(root, "type", "runtime_update");
    cJSON_AddNumberToObject(root, "timestamp", (double)(esp_timer_get_time() / 1000));

    cJSON *runtime_obj = cJSON_CreateObject();
    if (runtime_obj == NULL) {
        cJSON_Delete(root);
        return false;
    }
    cJSON_AddItemToObject(root, "runtime", runtime_obj);
    cJSON_AddStringToObject(runtime_obj, "state",
                            runtime_state_to_json_string(runtime->state));
    cJSON_AddNumberToObject(runtime_obj, "state_code", runtime->state);
    cJSON_AddNumberToObject(runtime_obj, "pc", runtime->pc);
    cJSON_AddStringToObject(runtime_obj, "step_type",
                            block_type_to_json_string(runtime->step_type));
    cJSON_AddNumberToObject(runtime_obj, "updated_at_ms",
                            (double)runtime->updated_at_ms);

    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json_string == NULL) {
        return false;
    }

    size_t json_len = strlen(json_string);
    if (json_len + 2u > out_size) {
        free(json_string);
        return false;
    }

    memcpy(out, json_string, json_len);
    out[json_len] = '\n';
    out[json_len + 1u] = '\0';
    *out_len = json_len + 1u;
    free(json_string);
    return true;
}

static bool runtime_snapshot_equals(const brain_runtime_snapshot_t *a,
                                    const brain_runtime_snapshot_t *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }

    return a->state == b->state &&
           a->pc == b->pc &&
           a->step_type == b->step_type &&
           a->updated_at_ms == b->updated_at_ms;
}

static void block_config_scan_task(void *pvParameters) {
    (void)pvParameters;

    // Adaptive interval: scan fast around changes, back off when stable.
    const TickType_t fast_delay = pdMS_TO_TICKS(40);   /* Faster app reaction without returning to 10 ms hammering */
    const TickType_t max_delay = pdMS_TO_TICKS(BLOCK_CONFIG_SCAN_INTERVAL_MS);
    const TickType_t paused_delay = pdMS_TO_TICKS(250);
    TickType_t delay_ticks = fast_delay;
    int stable_scans = 0;

    while (1) {
        if (brain_executor_scan_pause_active()) {
            vTaskDelay(paused_delay);
            continue;
        }

        block_config_manager_scan();
        bool config_changed = block_config_manager_has_changed();

        if (config_changed) {
            ESP_LOGW(TAG, "Block configuration changed; resetting validation state");
            brain_event_handler_reset_validation();
            s_validation_requested_by_start = false;
            // Rescan fairly soon, but avoid hammering a long settling chain.
            delay_ticks = fast_delay;
            stable_scans = 0;
        } else {
            // No change: gradually back off up to max_delay to reduce bus traffic.
            if (delay_ticks < max_delay) {
                stable_scans++;
                if (stable_scans >= 4) { // every few stable scans, increase delay a bit
                    delay_ticks += pdMS_TO_TICKS(150);
                    if (delay_ticks > max_delay) {
                        delay_ticks = max_delay;
                    }
                    stable_scans = 0;
                }
            }
        }

        // Update cached JSON if changed or if we don't have a valid cache yet.
        if (config_changed || !s_block_config_json_valid) {
            if (block_config_manager_get_json(s_block_config_scan_json_buffer,
                                              sizeof(s_block_config_scan_json_buffer)) == ESP_OK) {
                size_t json_len = strlen(s_block_config_scan_json_buffer);
                // Ensure newline-terminated (desktop parser expects newline)
                if (json_len < sizeof(s_block_config_scan_json_buffer) - 1) {
                    s_block_config_scan_json_buffer[json_len] = '\n';
                    s_block_config_scan_json_buffer[json_len + 1] = '\0';
                    json_len += 1;
                }

                if (s_block_config_json_mutex != NULL &&
                    xSemaphoreTake(s_block_config_json_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                    if (json_len < sizeof(s_block_config_json)) {
                        memcpy(s_block_config_json, s_block_config_scan_json_buffer, json_len);
                        s_block_config_json[json_len] = '\0';
                        s_block_config_json_len = json_len;
                        s_block_config_json_valid = true;
                    }
                    xSemaphoreGive(s_block_config_json_mutex);
                }

                if (s_block_config_event_group != NULL) {
                    xEventGroupSetBits(s_block_config_event_group, BLOCK_CONFIG_CHANGED_BIT);
                }
            }
        }

        vTaskDelay(delay_ticks);
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        s_companion_connected = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_MS));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_LOGI(TAG, "Setting WiFi SSID %s", WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static bool brain_executor_scan_pause_active(void)
{
    const brain_executor_context_t *ctx = brain_executor_get_context();
    if (ctx == NULL) {
        return false;
    }

    return (ctx->state == EXECUTOR_RUNNING ||
            ctx->state == EXECUTOR_WAIT_DELAY ||
            ctx->state == EXECUTOR_WAIT_INPUT);
}

static void tcp_client_task(void *pvParameters)
{
    char rx_buffer[TCP_RX_BUF_SIZE];
    int sock = -1;
    struct sockaddr_in dest_addr;
    char json_buffer[BLOCK_CONFIG_JSON_BUFFER_SIZE];
    brain_runtime_snapshot_t last_sent_runtime = {0};
    bool last_sent_runtime_valid = false;

    while (1) {
        /* Wait for Wi‑Fi connection */
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                               pdFALSE, pdTRUE, portMAX_DELAY);
        if (!(bits & WIFI_CONNECTED_BIT)) {
            ESP_LOGW(TAG, "WiFi not connected, waiting...");
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_MS));
            continue;
        }

        ESP_LOGI(TAG, "Attempting to connect to server %s:%d", SERVER_IP, SERVER_PORT);

        /* Setup server address */
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_IP, &dest_addr.sin_addr.s_addr);

        /* Create socket */
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_MS));
            continue;
        }

        /* Short recv timeout so we wake often and push config updates quickly (~50 ms). */
        struct timeval rcv_timeout;
        rcv_timeout.tv_sec = 0;
        rcv_timeout.tv_usec = 50000;  /* 50 ms */
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rcv_timeout, sizeof(rcv_timeout));
        struct timeval snd_timeout;
        snd_timeout.tv_sec = 5;
        snd_timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &snd_timeout, sizeof(snd_timeout));

        /* Connect to server */
        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            close(sock);
            sock = -1;
            vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_MS));
            continue;
        }

        ESP_LOGI(TAG, "Successfully connected to server");
        s_companion_connected = true;

        // Brief settle delay before first send (helps when connection was flapping)
        vTaskDelay(pdMS_TO_TICKS(200));

        // Send most recent cached configuration immediately on connect.
        size_t json_len = 0;
        if (!copy_latest_block_config_json(json_buffer, sizeof(json_buffer), &json_len)) {
            // Wait briefly for the scan task to populate initial JSON.
            if (s_block_config_event_group != NULL) {
                xEventGroupWaitBits(s_block_config_event_group,
                                    BLOCK_CONFIG_CHANGED_BIT,
                                    pdTRUE,   // clear on exit
                                    pdFALSE,
                                    pdMS_TO_TICKS(1500));
            }
            (void)copy_latest_block_config_json(json_buffer, sizeof(json_buffer), &json_len);
        }

        if (json_len > 0) {
            int written = send(sock, json_buffer, json_len, 0);
            if (written < 0) {
                ESP_LOGE(TAG, "Error sending initial config: errno %d", errno);
            } else {
                ESP_LOGI(TAG, "Sent initial block configuration (%d bytes)", written);
            }
        } else {
            ESP_LOGW(TAG, "No cached block configuration available yet");
        }

        const brain_runtime_snapshot_t *initial_runtime = brain_event_handler_get_runtime_snapshot();
        if (initial_runtime != NULL) {
            char runtime_json[256];
            size_t runtime_len = 0;
            if (build_runtime_update_json(runtime_json, sizeof(runtime_json), &runtime_len) &&
                runtime_len > 0) {
                int written = send(sock, runtime_json, runtime_len, 0);
                if (written < 0) {
                    ESP_LOGE(TAG, "Error sending initial runtime update: errno %d", errno);
                } else {
                    last_sent_runtime = *initial_runtime;
                    last_sent_runtime_valid = true;
                }
            }
        }

        /* Main send/receive loop */
        while (1) {
            // Send updated config when scan task reports a change.
            if (s_block_config_event_group != NULL) {
                EventBits_t cfg_bits = xEventGroupWaitBits(s_block_config_event_group,
                                                          BLOCK_CONFIG_CHANGED_BIT,
                                                          pdTRUE,    // clear
                                                          pdFALSE,
                                                          0);
                if (cfg_bits & BLOCK_CONFIG_CHANGED_BIT) {
                    size_t updated_len = 0;
                    if (copy_latest_block_config_json(json_buffer, sizeof(json_buffer), &updated_len) &&
                        updated_len > 0) {
                        int written = send(sock, json_buffer, updated_len, 0);
                        if (written < 0) {
                            ESP_LOGE(TAG, "Error sending block config: errno %d", errno);
                            break; // will reconnect
                        } else {
                            ESP_LOGI(TAG, "Sent block configuration (%d bytes)", written);
                        }
                    }
                }
            }

            const brain_runtime_snapshot_t *runtime = brain_event_handler_get_runtime_snapshot();
            if (runtime != NULL &&
                (!last_sent_runtime_valid || !runtime_snapshot_equals(&last_sent_runtime, runtime))) {
                char runtime_json[256];
                size_t runtime_len = 0;
                if (build_runtime_update_json(runtime_json, sizeof(runtime_json), &runtime_len) &&
                    runtime_len > 0) {
                    int written = send(sock, runtime_json, runtime_len, 0);
                    if (written < 0) {
                        ESP_LOGE(TAG, "Error sending runtime update: errno %d", errno);
                        break;
                    }
                    last_sent_runtime = *runtime;
                    last_sent_runtime_valid = true;
                }
            }

            // --- Receive messages ---
            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (len > 0) {
                rx_buffer[len] = '\0';
                ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);

                // Try to parse as JSON first (for heartbeat messages)
                cJSON *json = cJSON_Parse(rx_buffer);
                if (json != NULL) {
                    cJSON *type_item = cJSON_GetObjectItem(json, "type");
                    if (type_item != NULL && cJSON_IsString(type_item)) {
                        const char *type = cJSON_GetStringValue(type_item);
                        
                        if (strcmp(type, "heartbeat") == 0) {
                            // Respond to heartbeat
                            ESP_LOGI(TAG, "Received heartbeat, sending acknowledgment");
                            cJSON *ack_json = cJSON_CreateObject();
                            cJSON_AddStringToObject(ack_json, "type", "heartbeat_ack");
                            cJSON_AddNumberToObject(ack_json, "timestamp", (double)(esp_timer_get_time() / 1000));
                            char *ack_string = cJSON_Print(ack_json);
                            if (ack_string != NULL) {
                                send(sock, ack_string, strlen(ack_string), 0);
                                send(sock, "\n", 1, 0);
                                free(ack_string);
                            }
                            cJSON_Delete(ack_json);
                            cJSON_Delete(json);
                            vTaskDelay(pdMS_TO_TICKS(100)); // Small delay before next iteration
                            continue;
                        } else if (strcmp(type, "config_validation") == 0) {
                            if (!s_validation_requested_by_start) {
                                ESP_LOGI(TAG, "Applying unsolicited config_validation");
                            }

                            cJSON *is_valid_item = cJSON_GetObjectItem(json, "is_valid");
                            cJSON *error_count_item = cJSON_GetObjectItem(json, "error_count");
                            cJSON *timestamp_item = cJSON_GetObjectItem(json, "timestamp");

                            bool is_valid = cJSON_IsBool(is_valid_item) && cJSON_IsTrue(is_valid_item);
                            uint32_t error_count = cJSON_IsNumber(error_count_item)
                                                   ? (uint32_t)cJSON_GetNumberValue(error_count_item)
                                                   : 0;
                            uint64_t timestamp_ms = cJSON_IsNumber(timestamp_item)
                                                    ? (uint64_t)cJSON_GetNumberValue(timestamp_item)
                                                    : (uint64_t)(esp_timer_get_time() / 1000);

                            brain_event_handler_set_config_validation(is_valid, error_count, timestamp_ms);
                            s_validation_requested_by_start = false;
                            ESP_LOGI(TAG, "Applied config_validation: valid=%s errors=%lu ts=%llu",
                                     is_valid ? "true" : "false",
                                     (unsigned long)error_count,
                                     (unsigned long long)timestamp_ms);

                            cJSON_Delete(json);
                            continue;
                        }
                    }
                    cJSON_Delete(json);
                }

                // Parse newline-delimited commands and act
                char *saveptr = NULL;
                char *line = strtok_r(rx_buffer, "\r\n", &saveptr);
                while (line) {
                    ESP_LOGI(TAG, "Command received: '%s'", line);

                    if (strcasecmp(line, "START") == 0) {
                        s_validation_requested_by_start = true;
                        const brain_validation_state_t *validation = brain_event_handler_get_validation_state();
                        bool can_start = brain_event_handler_can_start_execution();

                        if (!can_start) {
                            if (validation != NULL && !validation->has_received_validation) {
                                const char *nak = "NAK:NEED_VALIDATION\n";
                                ESP_LOGW(TAG, "START blocked: waiting for config_validation");
                                send(sock, nak, strlen(nak), 0);
                            } else {
                                const char *nak = "NAK:INVALID_CONFIG\n";
                                ESP_LOGW(TAG, "START blocked: config validation is invalid");
                                send(sock, nak, strlen(nak), 0);
                            }
                        } else {
                            bool handled = brain_event_handle_message(line);
                            if (handled) {
                                const char *ack = "ACK:START\n";
                                send(sock, ack, strlen(ack), 0);
                            } else {
                                ESP_LOGW(TAG, "START rejected: event queue not ready/full");
                                const char *nak = "NAK:INVALID_STATE\n";
                                send(sock, nak, strlen(nak), 0);
                            }
                        }
                    } else if (strcasecmp(line, "STOP") == 0) {
                        bool handled = brain_event_handle_message(line);
                        if (handled) {
                            const char *ack = "ACK:STOP\n";
                            send(sock, ack, strlen(ack), 0);
                        } else {
                            ESP_LOGW(TAG, "STOP rejected: event queue not ready/full");
                            const char *nak = "NAK:INVALID_STATE\n";
                            send(sock, nak, strlen(nak), 0);
                        }
                    } else {
                        bool handled = brain_event_handle_message(line);
                        if (handled) {
                            const char *ack = "ACK:EVENT\n";
                            send(sock, ack, strlen(ack), 0);
                        } else {
                            ESP_LOGW(TAG, "Unknown or rejected command: '%s'", line);
                            const char *nak = "NAK:UNKNOWN\n";
                            send(sock, nak, strlen(nak), 0);
                        }
                    }

                    line = strtok_r(NULL, "\r\n", &saveptr);
                }

            } else if (len == 0) {
                ESP_LOGW(TAG, "Connection closed by peer");
                s_companion_connected = false;
                break;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    ESP_LOGD(TAG, "Receive timeout (no data)");
                } else {
                    ESP_LOGE(TAG, "recv failed: errno %d", errno);
                    s_companion_connected = false;
                    break;
                }
            }

            /* Short delay when idle; recv timeout already throttles the loop */
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        /* Cleanup socket on disconnect */
        if (sock != -1) {
            close(sock);
            sock = -1;
        }
        s_companion_connected = false;
        last_sent_runtime_valid = false;

        ESP_LOGI(TAG, "Disconnected, reconnecting in %d ms", TCP_RETRY_MS);
        vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_MS));
    }

    vTaskDelete(NULL);
}

bool brain_companion_is_connected(void)
{
    return s_companion_connected;
}

void start_network_client(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize block configuration manager
    block_config_manager_init();
    brain_event_handler_init();

    // Init shared config cache + start scan task (separate from TCP).
    if (s_block_config_event_group == NULL) {
        s_block_config_event_group = xEventGroupCreate();
    }
    if (s_block_config_json_mutex == NULL) {
        s_block_config_json_mutex = xSemaphoreCreateMutex();
    }
    xTaskCreatePinnedToCore(block_config_scan_task,
                            "block_cfg_scan",
                            BLOCK_CONFIG_SCAN_TASK_STACK_SIZE,
                            NULL,
                            4,
                            NULL,
                            0);

    wifi_init_sta();

    /* Start TCP client task on Core 0 to keep Core 1 available for GUI. */
    xTaskCreatePinnedToCore(tcp_client_task, "tcp_client_task", 8192, NULL, 5, NULL, 0);
}
