// Brain block event handler skeleton.
// Implement message parsing + routing here.

#include "brain_event_handler.h"

#include "esp_log.h"

static const char *TAG = "brain_evt";

void brain_event_handler_init(void) {
    // TODO: initialize queues, state, or subscriptions
    ESP_LOGI(TAG, "brain_event_handler_init (skeleton)");
}

void brain_event_handle_message(const char *message) {
    (void)message;
    // TODO: parse app/host messages and route to handlers
}

void brain_event_handle_block_event(uint8_t block_addr,
                                    uint8_t event_id,
                                    const uint8_t *payload,
                                    size_t payload_len) {
    (void)block_addr;
    (void)event_id;
    (void)payload;
    (void)payload_len;
    // TODO: react to block-side events
}
