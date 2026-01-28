#pragma once
#include "board.h"

typedef struct {
    void (*on_enter)(void);
    void (*on_exit)(void);
    void (*tick)(void);
    void (*on_key)(board_key_t key);
} ui_page_t;
