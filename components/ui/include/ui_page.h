#pragma once
#include "board.h"

typedef struct {
    void (*on_enter)(void);
    void (*on_exit)(void);
    uint32_t (*update)(void); // Returns sleep duration in ms. If 0, default to 1000.
    void (*render)(void);
    void (*on_key)(board_key_t key);
} ui_page_t;
