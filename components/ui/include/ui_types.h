#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_MESSAGES 10

typedef struct {
    char sender[32];
    char text[128];
    uint32_t timestamp;
    bool is_read;
} ui_message_t;

#ifdef __cplusplus
}
#endif
