#include "ble_protocol.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "ble_protocol";

static uint8_t calculate_checksum(const uint8_t *data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

bool ble_protocol_parse(const uint8_t *data, uint16_t len, ble_parsed_msg_t *out_msg) {
    if (!data || !out_msg || len < 9) {
        return false;
    }

    // 1. Checksum Validation
    uint8_t received_checksum = data[len - 1];
    uint8_t calculated_checksum = calculate_checksum(data, len - 1);
    if (received_checksum != calculated_checksum) {
        ESP_LOGE(TAG, "Checksum failed: Recv 0x%02X, Calc 0x%02X", received_checksum, calculated_checksum);
        return false;
    }

    // 2. Protocol Version
    if (data[0] != PROTOCOL_VERSION) {
        ESP_LOGE(TAG, "Invalid Protocol Version: 0x%02X", data[0]);
        return false;
    }

    // 3. Command Type
    if (data[1] != CMD_TYPE_MESSAGE) {
        ESP_LOGW(TAG, "Unsupported Command Type: 0x%02X", data[1]);
        return false;
    }

    memset(out_msg, 0, sizeof(ble_parsed_msg_t));
    strcpy(out_msg->sender, "App"); // Default sender

    // Offset tracking
    size_t offset = 4; // Skip Ver, Type, Seq(2)

    // 4. Colors
    uint8_t color_count = data[offset++];
    if (offset + color_count * 3 > len) return false;

    if (color_count > 0) {
        out_msg->effect.r = data[offset];
        out_msg->effect.g = data[offset + 1];
        out_msg->effect.b = data[offset + 2];
        out_msg->effect.duration_ms = 3000;
    }
    offset += color_count * 3;

    // 5. Vibration
    if (offset + 2 > len) return false;
    out_msg->vib_mode = data[offset++];
    uint8_t vib_strength = data[offset++]; // Ignored for now
    (void)vib_strength;

    // 6. Text
    if (offset + 1 > len) return false;
    uint8_t text_len = data[offset++];
    if (offset + text_len > len) return false;

    if (text_len > 64) text_len = 64;
    memcpy(out_msg->message, &data[offset], text_len);
    out_msg->message[text_len] = '\0';
    offset += text_len;

    // 7. Screen Effect
    if (offset + 1 > len) return false;
    out_msg->screen_effect = data[offset++];

    return true;
}
