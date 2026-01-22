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

#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "brain_block.h" // exposes i2c helpers and device registry
#include "block_config_manager.h"
#include "cJSON.h"

#define WIFI_SSID       "WhiteSky-Accolade" // <-- Set your Wi‑Fi SSID here
#define WIFI_PASS       "34bwheaf"       // <-- Set your Wi‑Fi password here

/* Desktop server IP and port to connect to (set to your desktop listening server) */
#define SERVER_IP       "100.110.161.96" // <-- Set your server's IP address here (ipconfig)
#define SERVER_PORT     41233

/* reconnect / timing settings */
#define WIFI_RETRY_MS         5000
#define TCP_RETRY_MS          2000
#define TCP_SEND_INTERVAL_MS  5000
#define TCP_RX_BUF_SIZE       512
#define BLOCK_CONFIG_SCAN_INTERVAL_MS  3000  // Scan every 3 seconds
#define BLOCK_CONFIG_JSON_BUFFER_SIZE  2048  // JSON buffer size

static const char *TAG = "brain_block";
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
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

static void tcp_client_task(void *pvParameters)
{
    char rx_buffer[TCP_RX_BUF_SIZE];
    int sock = -1;
    struct sockaddr_in dest_addr;

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

        /* Set connect/send/recv timeouts */
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

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

        // Perform initial scan and send configuration
        block_config_manager_scan();
        char json_buffer[BLOCK_CONFIG_JSON_BUFFER_SIZE];
        if (block_config_manager_get_json(json_buffer, sizeof(json_buffer)) == ESP_OK) {
            size_t json_len = strlen(json_buffer);
            if (json_len < sizeof(json_buffer) - 1) {
                json_buffer[json_len] = '\n';
                json_buffer[json_len + 1] = '\0';
            }
            int written = send(sock, json_buffer, strlen(json_buffer), 0);
            if (written < 0) {
                ESP_LOGE(TAG, "Error sending initial config: errno %d", errno);
            } else {
                ESP_LOGI(TAG, "Sent initial block configuration (%d bytes)", written);
            }
        }

        TickType_t last_scan_time = xTaskGetTickCount();
        bool initial_sent = true;

        /* Main send/receive loop */
        while (1) {
            // --- Periodic block configuration scan ---
            TickType_t current_time = xTaskGetTickCount();
            if ((current_time - last_scan_time) >= pdMS_TO_TICKS(BLOCK_CONFIG_SCAN_INTERVAL_MS)) {
                block_config_manager_scan();
                last_scan_time = current_time;

                // Send configuration if it changed or if this is the first scan after connection
                if (block_config_manager_has_changed() || !initial_sent) {
                    if (block_config_manager_get_json(json_buffer, sizeof(json_buffer)) == ESP_OK) {
                        size_t json_len = strlen(json_buffer);
                        if (json_len < sizeof(json_buffer) - 1) {
                            json_buffer[json_len] = '\n';
                            json_buffer[json_len + 1] = '\0';
                        }
                        int written = send(sock, json_buffer, strlen(json_buffer), 0);
                        if (written < 0) {
                            ESP_LOGE(TAG, "Error sending block config: errno %d", errno);
                            break; // will reconnect
                        } else {
                            ESP_LOGI(TAG, "Sent block configuration (%d bytes)", written);
                        }
                    }
                    initial_sent = true;
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
                                strcat(ack_string, "\n");
                                send(sock, ack_string, strlen(ack_string), 0);
                                free(ack_string);
                            }
                            cJSON_Delete(ack_json);
                            cJSON_Delete(json);
                            vTaskDelay(pdMS_TO_TICKS(100)); // Small delay before next iteration
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
                        ESP_LOGI(TAG, "Handling START: instructing Child 1");
                        // Send command to demo task via queue
                        demo_cmd_t cmd = CMD_START;
                        xQueueSend(demo_cmd_queue, &cmd, 0);


                        // Acknowledge to server/app
                        const char *ack = "ACK:START\n";
                        send(sock, ack, strlen(ack), 0);
                    } else if (strcasecmp(line, "STOP") == 0) {
                        ESP_LOGI(TAG, "Handling STOP: clearing Child 1");
                        demo_cmd_t cmd = CMD_STOP;
                        xQueueSend(demo_cmd_queue, &cmd, 0);


                        const char *ack = "ACK:STOP\n";
                        send(sock, ack, strlen(ack), 0);
                    } else {
                        ESP_LOGW(TAG, "Unknown command: '%s'", line);
                        const char *nak = "NAK:UNKNOWN\n";
                        send(sock, nak, strlen(nak), 0);
                    }

                    line = strtok_r(NULL, "\r\n", &saveptr);
                }

            } else if (len == 0) {
                ESP_LOGW(TAG, "Connection closed by peer");
                break;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    ESP_LOGD(TAG, "Receive timeout (no data)");
                } else {
                    ESP_LOGE(TAG, "recv failed: errno %d", errno);
                    break;
                }
            }

            // Small delay to prevent tight loop
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        /* Cleanup socket on disconnect */
        if (sock != -1) {
            close(sock);
            sock = -1;
        }

        ESP_LOGI(TAG, "Disconnected, reconnecting in %d ms", TCP_RETRY_MS);
        vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_MS));
    }

    vTaskDelete(NULL);
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

    wifi_init_sta();

    /* Start TCP client task */
    xTaskCreate(tcp_client_task, "tcp_client_task", 8192, NULL, 5, NULL);
}
