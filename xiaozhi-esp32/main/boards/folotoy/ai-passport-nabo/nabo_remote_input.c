#include "nabo_remote_input.h"

void nabo_remote_reset(nabo_remote_input_t* input) { *input = (nabo_remote_input_t){.held = -1}; }

nabo_key_t nabo_remote_press(nabo_remote_input_t* input, muse_menu_key_t key, bool pressed,
                             uint32_t now) {
    if (key != MUSE_MENU_UP && key != MUSE_MENU_DOWN)
        return NABO_KEY_COUNT;
    if (!pressed) {
        if (input->held == (int)key)
            input->held = -1;
        return NABO_KEY_COUNT;
    }
    input->held = key;
    input->since_ms = now;
    input->next_ms = now + 450;
    input->up_pending = key == MUSE_MENU_UP;
    input->up_repeated = false;
    /* UP taps wait for SINGLE_CLICK/DOUBLE_CLICK. A double click must not
     * scroll the current reading page twice before navigating left. */
    return key == MUSE_MENU_DOWN ? NABO_KEY_SCROLL_DOWN : NABO_KEY_COUNT;
}

nabo_key_t nabo_remote_click(nabo_remote_input_t* input, muse_menu_key_t key) {
    if (key == MUSE_MENU_LEFT || key == MUSE_MENU_SELECT) {
        nabo_remote_reset(input);
        return key == MUSE_MENU_LEFT ? NABO_KEY_LEFT : NABO_KEY_RIGHT;
    }
    if (key == MUSE_MENU_UP) {
        bool scroll = input->up_pending && !input->up_repeated;
        input->up_pending = false;
        return scroll ? NABO_KEY_SCROLL_UP : NABO_KEY_COUNT;
    }
    return NABO_KEY_COUNT;
}

nabo_key_t nabo_remote_tick(nabo_remote_input_t* input, uint32_t now) {
    if (input->held < 0)
        return NABO_KEY_COUNT;
    if (now - input->since_ms > 15000) {
        nabo_remote_reset(input);
        return NABO_KEY_COUNT;
    }
    if ((int32_t)(now - input->next_ms) < 0)
        return NABO_KEY_COUNT;
    input->next_ms = now + 160;
    if (input->held == MUSE_MENU_UP) {
        input->up_repeated = true;
        return NABO_KEY_SCROLL_UP;
    }
    return NABO_KEY_SCROLL_DOWN;
}
