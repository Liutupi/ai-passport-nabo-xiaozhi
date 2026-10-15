#pragma once
#include "muse_menu.h"
#include "nabo_keys_model.h"

typedef struct {
    int held;
    uint32_t since_ms, next_ms;
    bool up_pending, up_repeated;
} nabo_remote_input_t;

void nabo_remote_reset(nabo_remote_input_t* input);
nabo_key_t nabo_remote_press(nabo_remote_input_t* input, muse_menu_key_t key, bool pressed,
                             uint32_t now);
nabo_key_t nabo_remote_click(nabo_remote_input_t* input, muse_menu_key_t key);
nabo_key_t nabo_remote_tick(nabo_remote_input_t* input, uint32_t now);
