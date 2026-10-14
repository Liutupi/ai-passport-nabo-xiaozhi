#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "muse_menu.h"

/* UI-thread state only. Core requests are nonblocking; closing never stops audio. */
void nabo_radio_menu_open(void);
void nabo_radio_menu_close(void);
bool nabo_radio_menu_key(muse_menu_key_t key);
void nabo_radio_menu_text(char *out, size_t cap);
const char *nabo_radio_menu_action(void);
bool nabo_radio_menu_waiting(void);
