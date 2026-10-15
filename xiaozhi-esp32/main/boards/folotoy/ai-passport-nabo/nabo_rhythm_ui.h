#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "muse_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "节奏回声 ECHO CLUB": objects exist only while the program is open. */
bool nabo_rhythm_ui_open(lv_obj_t* screen, const lv_font_t* cjk_font);
void nabo_rhythm_ui_close(void);
void nabo_rhythm_ui_tick(void);
/* false: the key was not used (caller may navigate away). */
bool nabo_rhythm_ui_key(muse_menu_key_t key);
/* Press-down timestamp of a hit key: lane 0 = OK, lane 1 = DOWN. */
void nabo_rhythm_ui_press(int lane, uint32_t event_ms);
bool nabo_rhythm_ui_in_song(void);
/* Development aid: forget every saved level result. */
void nabo_rhythm_ui_reset_best(void);

#ifdef __cplusplus
}
#endif
