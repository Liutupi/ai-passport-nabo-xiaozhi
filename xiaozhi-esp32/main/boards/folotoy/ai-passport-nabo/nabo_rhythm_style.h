#pragma once
/* Shared poster palette and LVGL helpers for the rhythm screens. */
#include "lvgl.h"
#include "nabo_rhythm_model.h"

#define RH_PAPER 0xEFEBE2
#define RH_GRID 0xE2DDD2
#define RH_INK 0x161616
#define RH_MUTED 0x8C867B
#define RH_WHITE 0xFFFFFF
#define RH_ORANGE 0xFF4A1C
#define RH_BLUE 0x2747FF
#define RH_PINK 0xE5226B
#define RH_YELLOW 0xFFC21A

LV_FONT_DECLARE(nabo_poster_font_28);
LV_FONT_DECLARE(nabo_poster_font_48);
LV_FONT_DECLARE(nabo_mono_font_12);

extern const lv_font_t* rh_cjk;
extern const char* const rh_roman[RH_LEVELS];

lv_obj_t* rh_box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color);
lv_obj_t* rh_text(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* value, int x,
                  int y);
lv_obj_t* rh_chip(lv_obj_t* parent, const char* value, uint32_t bg, uint32_t fg, int x, int y);
void rh_set_text(lv_obj_t* label, const char* value);
uint32_t rh_mode_color(int mode);
const char* rh_mode_name(int mode);

/* Play screen (nabo_rhythm_stage.c). */
void rh_stage_build(lv_obj_t* parent);
void rh_stage_begin(const nabo_rhythm_t* game);
/* Advances visuals; true once the song and its tail have finished. */
bool rh_stage_tick(nabo_rhythm_t* game);
void rh_stage_press(nabo_rhythm_t* game, int lane, int64_t sample);
void rh_stage_forget(void);
