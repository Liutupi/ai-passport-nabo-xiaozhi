#include "nabo_muyu_ui.h"
#include "src/draw/lv_image_decoder_private.h"
#include "nabo_muyu.h"
#include "nabo_muyu_assets.h"
#include "nabo_muyu_codec.h"
#include "esp_timer.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define STRIP_ROWS 4
#define STRIPS 2
#define FRAME_US 50000LL
#define FEEDBACK_US 450000LL

typedef struct { lv_draw_buf_t *buffer; unsigned frame; bool busy; } strip_t;
static strip_t s_strips[STRIPS];
static lv_image_dsc_t s_sources[NABO_MUYU_FRAME_COUNT];
static lv_obj_t *s_panel, *s_count, *s_sprite, *s_feedback, *s_sound;
static bool s_ready, s_active, s_animating, s_count_valid;
static int s_frame = -1;
static uint64_t s_shown_count = UINT64_MAX;
static int s_shown_sound = -1;
static int64_t s_hit_at;

static int source_frame(const void *source)
{
    for (unsigned i = 0; i < NABO_MUYU_FRAME_COUNT; ++i)
        if (source == &s_sources[i]) return (int)i;
    return -1;
}

static lv_result_t sprite_info(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc,
                               lv_image_header_t *header)
{
    (void)dec;
    int frame = source_frame(dsc->src);
    if (frame < 0) return LV_RESULT_INVALID;
    *header = s_sources[frame].header;
    return LV_RESULT_OK;
}

static lv_result_t sprite_open(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc)
{
    (void)dec;
    int frame = source_frame(dsc->src);
    if (frame < 0) return LV_RESULT_INVALID;
    for (unsigned i = 0; i < STRIPS; ++i) {
        if (!s_strips[i].busy && s_strips[i].buffer) {
            s_strips[i].busy = true;
            s_strips[i].frame = (unsigned)frame;
            dsc->user_data = &s_strips[i];
            return LV_RESULT_OK;
        }
    }
    return LV_RESULT_INVALID;
}

static lv_result_t sprite_get_area(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc,
                                   const lv_area_t *full, lv_area_t *area)
{
    (void)dec;
    strip_t *strip = dsc->user_data;
    if (!strip || !strip->busy || full->x1 < 0 || full->y1 < 0 ||
        full->x2 >= NABO_MUYU_WIDTH || full->y2 >= NABO_MUYU_HEIGHT ||
        full->x1 > full->x2 || full->y1 > full->y2) return LV_RESULT_INVALID;
    int y1 = area->y1 == LV_COORD_MIN ? full->y1 : area->y2 + 1;
    if (y1 > full->y2) return LV_RESULT_INVALID;
    int y2 = y1 + STRIP_ROWS - 1;
    if (y2 > full->y2) y2 = full->y2;
    int width = full->x2 - full->x1 + 1;
    lv_draw_buf_t *buffer = lv_draw_buf_reshape(strip->buffer, LV_COLOR_FORMAT_RGB565,
                                              width, y2 - y1 + 1, LV_STRIDE_AUTO);
    if (!buffer) return LV_RESULT_INVALID;
    size_t stride = buffer->header.stride / sizeof(uint16_t);
    for (int y = y1; y <= y2; ++y) {
        uint16_t *row = (uint16_t *)buffer->data + (size_t)(y - y1) * stride;
        if (!nabo_muyu_draw_row(strip->frame, (unsigned)y, full->x1, full->x2, row, stride))
            memset(row, 0, (size_t)width * sizeof(*row));
    }
    *area = (lv_area_t){ full->x1, y1, full->x2, y2 };
    dsc->decoded = buffer;
    return LV_RESULT_OK;
}

static void sprite_close(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc)
{
    (void)dec;
    for (unsigned i = 0; i < STRIPS; ++i)
        if (dsc->user_data == &s_strips[i]) s_strips[i].busy = false;
    dsc->user_data = NULL;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *value)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, value);
    return label;
}

void nabo_muyu_ui_build(lv_obj_t *parent, const lv_font_t *font)
{
    if (s_panel) return;
    s_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, 240, 320);
    lv_obj_set_style_bg_color(s_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(text(s_panel, font, 0xa99c83, "功德木鱼"), LV_ALIGN_TOP_MID, 0, 15);
    s_count = text(s_panel, font, 0xf4deb1, "累计功德\n0");
    lv_obj_set_style_text_line_space(s_count, 6, 0);
    lv_obj_align(s_count, LV_ALIGN_TOP_MID, 0, 47);
    s_sprite = lv_image_create(s_panel);
    lv_obj_set_pos(s_sprite, (240 - NABO_MUYU_WIDTH) / 2, 105);
    lv_obj_set_size(s_sprite, NABO_MUYU_WIDTH, NABO_MUYU_HEIGHT);
    s_feedback = text(s_panel, font, 0xf5d27e, "+1");
    lv_obj_set_pos(s_feedback, 191, 108);
    lv_obj_add_flag(s_feedback, LV_OBJ_FLAG_HIDDEN);
    s_sound = text(s_panel, font, 0xb4a48a, "音效：开");
    lv_obj_align(s_sound, LV_ALIGN_TOP_MID, 0, 247);
    lv_obj_t *hint = text(s_panel, font, 0xe3d6bf, "上：返回    下：音效\nOK：敲一下");
    lv_obj_set_style_text_line_space(hint, 7, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    for (unsigned i = 0; i < NABO_MUYU_FRAME_COUNT; ++i) {
        s_sources[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_sources[i].header.cf = LV_COLOR_FORMAT_RGB565;
        s_sources[i].header.w = NABO_MUYU_WIDTH;
        s_sources[i].header.h = NABO_MUYU_HEIGHT;
        s_sources[i].header.stride = NABO_MUYU_WIDTH * sizeof(uint16_t);
        /* The custom decoder identifies descriptors; pixels stay in Flash. */
        s_sources[i].data = (const uint8_t *)&s_sources[i];
    }
    for (unsigned i = 0; i < STRIPS; ++i) {
        s_strips[i].buffer = lv_draw_buf_create(NABO_MUYU_WIDTH, STRIP_ROWS,
                                               LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
        if (!s_strips[i].buffer) return;
    }
    lv_image_decoder_t *decoder = lv_image_decoder_create();
    if (!decoder) return;
    lv_image_decoder_set_info_cb(decoder, sprite_info);
    lv_image_decoder_set_open_cb(decoder, sprite_open);
    lv_image_decoder_set_get_area_cb(decoder, sprite_get_area);
    lv_image_decoder_set_close_cb(decoder, sprite_close);
    s_ready = true;
}

bool nabo_muyu_ui_open(void)
{
    if (!s_ready) return false;
    nabo_muyu_open();
    s_active = true;
    s_animating = false;
    s_frame = -1;
    s_shown_count = UINT64_MAX;
    s_count_valid = false;
    s_shown_sound = -1;
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_panel);
    nabo_muyu_ui_tick();
    return true;
}

void nabo_muyu_ui_close(void)
{
    if (!s_active) return;
    s_active = false;
    s_animating = false;
    nabo_muyu_close();
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
}

bool nabo_muyu_ui_key(muse_menu_key_t key)
{
    if (!s_active) return false;
    if (key == MUSE_MENU_UP || key == MUSE_MENU_BACK || key == MUSE_MENU_LEFT) return false;
    if (key == MUSE_MENU_DOWN) nabo_muyu_set_sound(!nabo_muyu_sound_enabled());
    else if (key == MUSE_MENU_SELECT && nabo_muyu_hit()) {
        s_hit_at = esp_timer_get_time();
        s_animating = true;
    }
    nabo_muyu_ui_tick();
    return true;
}

void nabo_muyu_ui_tick(void)
{
    if (!s_active) return;
    uint64_t count = nabo_muyu_count();
    if (!s_count_valid || count != s_shown_count) {
        char value[72];
        snprintf(value, sizeof(value), "累计功德\n%" PRIu64, count);
        lv_label_set_text(s_count, value);
        s_shown_count = count;
        s_count_valid = true;
    }
    int sound = nabo_muyu_sound_enabled();
    if (sound != s_shown_sound) {
        lv_label_set_text(s_sound, sound ? "音效：开" : "音效：关");
        s_shown_sound = sound;
    }
    int64_t age = esp_timer_get_time() - s_hit_at;
    if (age < 0) age = 0;
    int frame = s_animating && age < (NABO_MUYU_FRAME_COUNT - 1) * FRAME_US ?
                1 + (int)(age / FRAME_US) : 0;
    if (frame != s_frame) {
        lv_image_set_src(s_sprite, &s_sources[frame]);
        lv_obj_invalidate(s_sprite);
        s_frame = frame;
    }
    bool feedback = s_animating && age < FEEDBACK_US;
    lv_obj_set_flag(s_feedback, LV_OBJ_FLAG_HIDDEN, !feedback);
    if (feedback) lv_obj_set_pos(s_feedback, 191, 108 - (int)(age / 20000));
    if (s_animating && age >= FEEDBACK_US) s_animating = false;
}

bool nabo_muyu_ui_active(void) { return s_active; }
