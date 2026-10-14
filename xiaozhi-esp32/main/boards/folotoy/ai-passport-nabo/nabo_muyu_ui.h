#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "muse_menu.h"

/* LVGL thread only. Core calls publish requests, never touch codec/NVS here. */
void nabo_muyu_ui_build(lv_obj_t *parent, const lv_font_t *font);
bool nabo_muyu_ui_open(void);
void nabo_muyu_ui_close(void);
bool nabo_muyu_ui_key(muse_menu_key_t key);
void nabo_muyu_ui_tick(void);
bool nabo_muyu_ui_active(void);
