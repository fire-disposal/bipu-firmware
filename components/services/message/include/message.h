#pragma once

typedef struct {
    char sender[16];
    char text[128];
} bp_message_t;

// 消息存取 API
void message_init(void);
void message_store(const bp_message_t* msg);
const bp_message_t* message_latest(void);