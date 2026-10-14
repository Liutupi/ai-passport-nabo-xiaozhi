/* Dice page: a full felt tray drawn by a streaming LVGL image decoder.
 * Only the rectangle the dice and their shadows cover (old and new pose) is
 * invalidated each 40 ms frame. Strip buffers and the pose (about 4.5 KB in
 * all) exist only while the page is open; a closed page keeps just the last
 * seed, from which the same roll is rebuilt. */
#include "nabo_dice_ui.h"
#include "src/draw/lv_image_decoder_private.h"
#include "nabo_dice.h"
#include "nabo_dice_render.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRIP_ROWS 4
#define STRIPS 2
#define TRAY_X ((240 - NABO_DICE_W) / 2)
#define TRAY_Y 32

typedef struct { lv_draw_buf_t *buffer; bool busy; } strip_t;
static strip_t s_strips[STRIPS];
static lv_image_dsc_t s_source;
static lv_obj_t *s_panel, *s_sprite, *s_title, *s_total, *s_detail;
static bool s_built, s_active, s_rolling, s_have_result;
static unsigned s_count = 1, s_seed_count = 1;
static uint32_t s_seed;
static int64_t s_thrown_at;
static int s_dirty[NABO_DICE_MAX][4];
/* Per-throw cost, logged on landing: decoder time and pixels, UI frames. */
static int64_t s_draw_us;
static uint32_t s_draw_px, s_frames;
static unsigned s_dirty_count;
typedef struct { nabo_dice_plan_t plan; nabo_dice_scene_t scene; } state_t;
static state_t *s_state;
#define s_plan (s_state->plan)
#define s_scene (s_state->scene)

static lv_result_t dice_info(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc,
                             lv_image_header_t *header)
{
    (void)dec;
    if (dsc->src != &s_source) return LV_RESULT_INVALID;
    *header = s_source.header;
    return LV_RESULT_OK;
}

static lv_result_t dice_open(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc)
{
    (void)dec;
    if (dsc->src != &s_source) return LV_RESULT_INVALID;
    for (unsigned i = 0; i < STRIPS; ++i) {
        if (!s_strips[i].busy && s_strips[i].buffer) {
            s_strips[i].busy = true;
            dsc->user_data = &s_strips[i];
            return LV_RESULT_OK;
        }
    }
    return LV_RESULT_INVALID;
}

static lv_result_t dice_get_area(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc,
                                 const lv_area_t *full, lv_area_t *area)
{
    (void)dec;
    strip_t *strip = dsc->user_data;
    if (!s_state || !strip || !strip->busy || !strip->buffer || full->x1 < 0 || full->y1 < 0 ||
        full->x2 >= NABO_DICE_W || full->y2 >= NABO_DICE_H ||
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
    int64_t started = esp_timer_get_time();
    for (int y = y1; y <= y2; ++y)
        nabo_dice_draw_row(&s_scene, y, full->x1, full->x2,
                           (uint16_t *)buffer->data + (size_t)(y - y1) * stride);
    s_draw_us += esp_timer_get_time() - started;
    s_draw_px += (uint32_t)(width * (y2 - y1 + 1));
    *area = (lv_area_t){ full->x1, y1, full->x2, y2 };
    dsc->decoded = buffer;
    return LV_RESULT_OK;
}

static void dice_close(lv_image_decoder_t *dec, lv_image_decoder_dsc_t *dsc)
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

static void release(void)
{
    for (unsigned i = 0; i < STRIPS; ++i) {
        if (s_strips[i].buffer) lv_draw_buf_destroy(s_strips[i].buffer);
        s_strips[i].buffer = NULL;
        s_strips[i].busy = false;
    }
    free(s_state);
    s_state = NULL;
}

void nabo_dice_ui_build(lv_obj_t *parent, const lv_font_t *font)
{
    if (s_panel) return;
    s_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, 240, 320);
    lv_obj_set_style_bg_color(s_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    s_title = text(s_panel, font, 0xa99c83, "摇骰子");
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 9);
    s_sprite = lv_image_create(s_panel);
    lv_obj_set_pos(s_sprite, TRAY_X, TRAY_Y);
    lv_obj_set_size(s_sprite, NABO_DICE_W, NABO_DICE_H);
#if LV_FONT_MONTSERRAT_28
    s_total = text(s_panel, &lv_font_montserrat_28, 0xf5d27e, "");
#else
    s_total = text(s_panel, &lv_font_montserrat_20, 0xf5d27e, "");
#endif
    lv_obj_align(s_total, LV_ALIGN_TOP_MID, 0, TRAY_Y + NABO_DICE_H + 2);
    s_detail = text(s_panel, font, 0xe3d6bf, "");
    lv_obj_align(s_detail, LV_ALIGN_TOP_MID, 0, TRAY_Y + NABO_DICE_H + 34);
    lv_obj_t *hint = text(s_panel, font, 0xb4a48a,
                          "OK 摇   " LV_SYMBOL_DOWN " 颗数   " LV_SYMBOL_UP " 返回");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    s_source.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_source.header.cf = LV_COLOR_FORMAT_RGB565;
    s_source.header.w = NABO_DICE_W;
    s_source.header.h = NABO_DICE_H;
    s_source.header.stride = NABO_DICE_W * sizeof(uint16_t);
    /* The custom decoder identifies the descriptor; pixels are computed. */
    s_source.data = (const uint8_t *)&s_source;
    lv_image_decoder_t *decoder = lv_image_decoder_create();
    if (!decoder) return;
    lv_image_decoder_set_info_cb(decoder, dice_info);
    lv_image_decoder_set_open_cb(decoder, dice_open);
    lv_image_decoder_set_get_area_cb(decoder, dice_get_area);
    lv_image_decoder_set_close_cb(decoder, dice_close);
    s_built = true;
}

static void set_title(void)
{
    char value[32];
    snprintf(value, sizeof(value), "摇骰子（%u 颗）", s_count);
    lv_label_set_text(s_title, value);
}

static void show_result(void)
{
    char total[8], detail[48];
    unsigned n = s_plan.count, sum = nabo_dice_total(&s_plan);
    snprintf(total, sizeof(total), "%u", sum);
    if (n == 1) snprintf(detail, sizeof(detail), "点");   /* CJK16 has no ASCII: digits fall back to Montserrat */
    else {
        int w = snprintf(detail, sizeof(detail), "%u", s_plan.value[0]);
        for (unsigned i = 1; i < n && w > 0 && (size_t)w < sizeof(detail); ++i)
            w += snprintf(detail + w, sizeof(detail) - (size_t)w, " + %u", s_plan.value[i]);
        bool same = true;
        for (unsigned i = 1; i < n; ++i) same = same && s_plan.value[i] == s_plan.value[0];
        if (same && w > 0 && (size_t)w < sizeof(detail))
            snprintf(detail + w, sizeof(detail) - (size_t)w, n == 3 ? "  豹子！" : "  对子");
    }
    lv_label_set_text(s_total, total);
    lv_label_set_text(s_detail, detail);
    nabo_dice_settled(s_plan.value, n);
}

/* Per die, invalidate its old and new footprint (with shadow) together, in
 * screen pixels. Separate rectangles keep three spread dice from repainting
 * the whole tray; LVGL joins any that overlap. */
static void invalidate_dice(bool everything)
{
    int now[NABO_DICE_MAX][4];
    unsigned count = s_scene.count, span = count > s_dirty_count ? count : s_dirty_count;
    for (unsigned i = 0; i < count; ++i)
        nabo_dice_die_bounds(&s_scene, i, &now[i][0], &now[i][1], &now[i][2], &now[i][3]);
    if (everything) lv_obj_invalidate(s_sprite);
    else {
        lv_area_t at;
        lv_obj_get_coords(s_sprite, &at);
        for (unsigned i = 0; i < span; ++i) {
            int r[4];
            if (i < count) memcpy(r, now[i], sizeof(r));
            else memcpy(r, s_dirty[i], sizeof(r));
            if (i < count && i < s_dirty_count) {
                if (s_dirty[i][0] < r[0]) r[0] = s_dirty[i][0];
                if (s_dirty[i][1] < r[1]) r[1] = s_dirty[i][1];
                if (s_dirty[i][2] > r[2]) r[2] = s_dirty[i][2];
                if (s_dirty[i][3] > r[3]) r[3] = s_dirty[i][3];
            }
            lv_area_t area = { at.x1 + r[0], at.y1 + r[1], at.x1 + r[2], at.y1 + r[3] };
            lv_obj_invalidate_area(s_sprite, &area);
        }
    }
    memcpy(s_dirty, now, sizeof(now[0]) * count);
    s_dirty_count = count;
}

static void throw_dice(void)
{
    uint32_t seed = nabo_dice_new_seed();
    nabo_dice_plan(&s_plan, seed, s_count);
    s_seed = seed;
    s_seed_count = s_count;
    s_have_result = true;
    s_rolling = true;
    s_thrown_at = esp_timer_get_time();
    s_draw_us = 0;
    s_draw_px = s_frames = 0;
    nabo_dice_thrown(seed, s_count, s_thrown_at);
    set_title();
    lv_label_set_text(s_total, "");
    lv_label_set_text(s_detail, "");
    nabo_dice_ui_tick();
}

bool nabo_dice_ui_open(void)
{
    if (!s_built || s_active) return s_active;
    s_state = malloc(sizeof(*s_state));
    if (!s_state) return false;
    for (unsigned i = 0; i < STRIPS; ++i) {
        s_strips[i].buffer = lv_draw_buf_create(NABO_DICE_W, STRIP_ROWS,
                                               LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
        s_strips[i].busy = false;
        if (!s_strips[i].buffer) { release(); return false; }
    }
    nabo_dice_open();
    s_active = true;
    s_rolling = false;
    if (!s_seed) {
        /* A resting set to look at before the first throw. */
        s_seed = nabo_dice_new_seed();
        s_seed_count = s_count;
    }
    nabo_dice_plan(&s_plan, s_seed, s_seed_count);
    nabo_dice_scene(&s_scene, &s_plan, s_plan.end_ms);
    lv_image_set_src(s_sprite, &s_source);
    set_title();
    if (s_have_result) show_result();
    else {
        lv_label_set_text(s_total, "");
        lv_label_set_text(s_detail, "按 OK 摇一摇");
    }
    s_dirty_count = 0;
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_panel);
    invalidate_dice(true);
    return true;
}

void nabo_dice_ui_close(void)
{
    if (!s_active) return;
    s_active = false;
    if (s_rolling) {
        /* Leaving mid-throw settles it; the next visit shows that result. */
        s_rolling = false;
        nabo_dice_settled(s_plan.value, s_plan.count);
    }
    nabo_dice_close();
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    release();
}

bool nabo_dice_ui_key(muse_menu_key_t key)
{
    if (!s_active) return false;
    if (key == MUSE_MENU_UP || key == MUSE_MENU_BACK || key == MUSE_MENU_LEFT) return false;
    if (s_rolling) return true;                 /* let the dice land first */
    if (key == MUSE_MENU_DOWN) {
        s_count = s_count % NABO_DICE_MAX + 1;
        throw_dice();
    } else if (key == MUSE_MENU_SELECT || key == MUSE_MENU_RIGHT) {
        throw_dice();
    }
    return true;
}

void nabo_dice_ui_tick(void)
{
    if (!s_active || !s_rolling) return;
    int64_t age = (esp_timer_get_time() - s_thrown_at) / 1000;
    if (age < 0) age = 0;
    bool done = age >= s_plan.end_ms;
    nabo_dice_scene(&s_scene, &s_plan, done ? s_plan.end_ms : (int32_t)age);
    invalidate_dice(false);
    ++s_frames;
    if (done) {
        s_rolling = false;
        ESP_LOGI("nabo.dice", "throw: %u dice, %u frames in %u ms, decode %u ms total, %u px",
                 (unsigned)s_plan.count, (unsigned)s_frames, (unsigned)age,
                 (unsigned)(s_draw_us / 1000), (unsigned)s_draw_px);
        show_result();
    }
}

bool nabo_dice_ui_active(void) { return s_active; }
