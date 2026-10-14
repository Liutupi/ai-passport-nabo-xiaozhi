#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "muse_menu.h"

/* LVGL thread only. Publishes throws to nabo_dice; never touches codec/NVS. */
void nabo_dice_ui_build(lv_obj_t *parent, const lv_font_t *font);
bool nabo_dice_ui_open(void);
void nabo_dice_ui_close(void);
bool nabo_dice_ui_key(muse_menu_key_t key);
void nabo_dice_ui_tick(void);
bool nabo_dice_ui_active(void);
