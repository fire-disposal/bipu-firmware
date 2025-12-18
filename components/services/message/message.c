#include "message.h"

// 简单消息存储实现
static bp_message_t latest_msg;

void message_init(void) {
    latest_msg.sender[0] = 0;
    latest_msg.text[0] = 0;
}

void message_store(const bp_message_t* msg) {
    if (msg) {
        latest_msg = *msg;
    }
}

const bp_message_t* message_latest(void) {
    return &latest_msg;
}