/* 节奏回声 ECHO CLUB: level select, result card and key routing.
 * Original NABO design: listen to a one-bar call, answer it on the next bar. */
#include "nabo_rhythm_ui.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nabo_rhythm_play.h"
#include "nabo_rhythm_style.h"

#define TAG "NABO-Rhythm"
#define LEVEL_COUNT (RH_MODES * RH_LEVELS)
#define TILE_W 51
#define TILE_H 36

typedef enum { VIEW_SELECT, VIEW_PLAY, VIEW_RESULT } view_t;

const lv_font_t* rh_cjk;
const char* const rh_roman[RH_LEVELS] = {"I", "II", "III", "IV"};
static const char* const mode_cn[RH_MODES] = {"回声 · 单键跟拍", "对唱 · 高低双键",
                                              "混合 · 更快更密"};
static const char* const level_cn[RH_MODES][RH_LEVELS] = {
    {"四分音符", "八分音符", "长串连拍", "反拍切分"},
    {"高低交替", "快速换键", "连续换键", "反拍对唱"},
    {"单双键混合", "八分混合", "更快更密", "每句都变"}};

static lv_obj_t *s_root, *s_select, *s_play, *s_result;
static lv_obj_t *s_tiles[LEVEL_COUNT], *s_tile_grade[LEVEL_COUNT], *s_shadow, *s_info, *s_info_best;
static lv_obj_t *s_res_tag, *s_res_box, *s_res_grade, *s_res_title, *s_res_pct, *s_res_note;
static lv_obj_t *s_res_bars[4], *s_res_counts[4], *s_res_hint;
static view_t s_view;
static int s_cursor;
static uint8_t s_best[LEVEL_COUNT]; /* 0 = never played, else percent + 1 */
static nabo_rhythm_t s_game;
static bool s_passed;

lv_obj_t* rh_box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t* rh_text(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* value, int x,
                  int y) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    lv_label_set_text(label, value);
    return label;
}

lv_obj_t* rh_chip(lv_obj_t* parent, const char* value, uint32_t bg, uint32_t fg, int x, int y) {
    lv_obj_t* label = rh_text(parent, &nabo_mono_font_12, fg, value, x, y);
    lv_obj_set_style_bg_color(label, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(label, 5, 0);
    lv_obj_set_style_pad_ver(label, 2, 0);
    return label;
}

void rh_set_text(lv_obj_t* label, const char* value) {
    if (strcmp(lv_label_get_text(label), value))
        lv_label_set_text(label, value);
}

uint32_t rh_mode_color(int mode) {
    return mode == RH_MODE_ECHO ? RH_ORANGE : mode == RH_MODE_DUET ? RH_BLUE : RH_PINK;
}

const char* rh_mode_name(int mode) {
    return mode == RH_MODE_ECHO ? "ECHO" : mode == RH_MODE_DUET ? "DUET" : "REMIX";
}

static void load_best(void) {
    memset(s_best, 0, sizeof(s_best));
    nvs_handle_t h;
    if (nvs_open("nabo_ui", NVS_READONLY, &h) != ESP_OK)
        return;
    size_t size = sizeof(s_best);
    if (nvs_get_blob(h, "rh_best", s_best, &size) != ESP_OK || size != sizeof(s_best))
        memset(s_best, 0, sizeof(s_best));
    nvs_close(h);
}

static void save_best(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("nabo_ui", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "rh_best", s_best, sizeof(s_best));
        if (err == ESP_OK)
            err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK)
        ESP_LOGE(TAG, "best save failed: %d", err);
}

static void build_grid(lv_obj_t* parent) {
    /* Faint poster grid, 24 px pitch. */
    for (int x = 12; x < 240; x += 24)
        rh_box(parent, x, 0, 1, 320, RH_GRID);
    for (int y = 4; y < 320; y += 24)
        rh_box(parent, 0, y, 240, 1, RH_GRID);
}

static lv_obj_t* layer(void) {
    lv_obj_t* obj = rh_box(s_root, 0, 0, 240, 320, RH_PAPER);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    return obj;
}

static void build_select(void) {
    s_select = layer();
    rh_chip(s_select, "NABO RHYTHM", RH_INK, RH_WHITE, 12, 10);
    rh_chip(s_select, "12 LEVELS", RH_ORANGE, RH_WHITE, 104, 10);
    rh_text(s_select, &nabo_poster_font_28, RH_INK, "ECHO", 12, 33);
    rh_text(s_select, &nabo_poster_font_28, RH_ORANGE, "CLUB.", 12, 63);
    lv_obj_t* tagline = rh_text(s_select, rh_cjk, RH_INK, "先听示范\n再拍回去", 140, 42);
    lv_obj_set_style_text_line_space(tagline, 4, 0);
    s_shadow = rh_box(s_select, 0, 0, TILE_W, TILE_H, RH_INK);
    for (int m = 0; m < RH_MODES; ++m) {
        int y = 100 + m * 58;
        char text[24];
        snprintf(text, sizeof(text), "0%d %s", m + 1, rh_mode_name(m));
        rh_text(s_select, &nabo_mono_font_12, RH_INK, text, 12, y + 1);
        rh_text(s_select, rh_cjk, RH_MUTED, mode_cn[m], 86, y - 2);
        for (int l = 0; l < RH_LEVELS; ++l) {
            int index = m * RH_LEVELS + l;
            lv_obj_t* tile = rh_box(s_select, 12 + l * (TILE_W + 4), y + 17, TILE_W, TILE_H,
                                    rh_mode_color(m));
            lv_obj_set_style_border_color(tile, lv_color_hex(RH_INK), 0);
            rh_text(tile, &nabo_mono_font_12, RH_WHITE, rh_roman[l], 5, 2);
            snprintf(text, sizeof(text), "%u", nabo_rhythm_bpm(m, l));
            rh_text(tile, &lv_font_montserrat_20, RH_WHITE, text, 5, 13);
            s_tile_grade[index] = rh_text(tile, &nabo_mono_font_12, RH_WHITE, "-", 0, 2);
            lv_obj_align(s_tile_grade[index], LV_ALIGN_TOP_RIGHT, -5, 2);
            s_tiles[index] = tile;
        }
    }
    s_info = rh_text(s_select, rh_cjk, RH_INK, "", 12, 276);
    s_info_best = rh_text(s_select, &nabo_mono_font_12, RH_INK, "", 0, 279);
    lv_obj_align(s_info_best, LV_ALIGN_TOP_RIGHT, -12, 279);
    rh_box(s_select, 12, 297, 216, 1, RH_INK);
    rh_text(s_select, rh_cjk, RH_MUTED, "OK开始  上下选关  双击上返回", 12, 300);
}

static void show_select(void) {
    for (int i = 0; i < LEVEL_COUNT; ++i) {
        char grade[4] = "-";
        if (s_best[i])
            grade[0] = nabo_rhythm_grade(s_best[i] - 1);
        rh_set_text(s_tile_grade[i], grade);
        bool on = i == s_cursor;
        lv_obj_set_style_border_width(s_tiles[i], on ? 2 : 0, 0);
        lv_obj_set_style_bg_opa(s_tiles[i], on || !s_best[i] ? LV_OPA_COVER : LV_OPA_90, 0);
    }
    lv_obj_t* tile = s_tiles[s_cursor];
    lv_obj_set_pos(s_shadow, lv_obj_get_style_x(tile, 0) + 4, lv_obj_get_style_y(tile, 0) + 4);
    int mode = s_cursor / RH_LEVELS, level = s_cursor % RH_LEVELS;
    char text[64];
    snprintf(text, sizeof(text), "%s %s · %s", mode == 0 ? "回声" : mode == 1 ? "对唱" : "混合",
             rh_roman[level], level_cn[mode][level]);
    rh_set_text(s_info, text);
    if (s_best[s_cursor])
        snprintf(text, sizeof(text), "BEST %d%%", s_best[s_cursor] - 1);
    else
        snprintf(text, sizeof(text), "NEW");
    rh_set_text(s_info_best, text);
}

static void build_result(void) {
    s_result = layer();
    rh_chip(s_result, "RESULT", RH_INK, RH_WHITE, 12, 10);
    s_res_tag = rh_chip(s_result, "ECHO I", RH_ORANGE, RH_WHITE, 72, 10);
    lv_obj_t* shadow = rh_box(s_result, 16, 40, 92, 92, RH_INK);
    (void)shadow;
    s_res_box = rh_box(s_result, 12, 36, 92, 92, RH_ORANGE);
    s_res_grade = rh_text(s_res_box, &nabo_poster_font_48, RH_WHITE, "S", 0, 0);
    lv_obj_center(s_res_grade);
    s_res_title = rh_text(s_result, &nabo_poster_font_28, RH_INK, "CLEAR!", 118, 40);
    s_res_pct = rh_text(s_result, &nabo_poster_font_28, RH_ORANGE, "0%", 118, 78);
    static const char* const names[4] = {"PERFECT", "GOOD", "MISS", "EXTRA"};
    for (int i = 0; i < 4; ++i) {
        int y = 150 + i * 24;
        rh_text(s_result, &nabo_mono_font_12, RH_INK, names[i], 12, y);
        rh_box(s_result, 80, y + 3, 120, 10, RH_GRID);
        s_res_bars[i] = rh_box(s_result, 80, y + 3, 0, 10, RH_INK);
        s_res_counts[i] = rh_text(s_result, &nabo_mono_font_12, RH_INK, "0", 0, y);
        lv_obj_align(s_res_counts[i], LV_ALIGN_TOP_RIGHT, -12, y);
    }
    s_res_note = rh_text(s_result, rh_cjk, RH_INK, "", 12, 252);
    rh_box(s_result, 12, 277, 216, 1, RH_INK);
    s_res_hint = rh_text(s_result, rh_cjk, RH_MUTED, "", 12, 280);
    lv_obj_set_style_text_line_space(s_res_hint, 1, 0);
}

static void show_view(view_t view) {
    s_view = view;
    lv_obj_set_flag(s_select, LV_OBJ_FLAG_HIDDEN, view != VIEW_SELECT);
    lv_obj_set_flag(s_play, LV_OBJ_FLAG_HIDDEN, view != VIEW_PLAY);
    lv_obj_set_flag(s_result, LV_OBJ_FLAG_HIDDEN, view != VIEW_RESULT);
}

static void start_level(int index) {
    s_cursor = index;
    nabo_rhythm_start(&s_game, index / RH_LEVELS, index % RH_LEVELS, esp_random());
    rh_stage_begin(&s_game);
    show_view(VIEW_PLAY);
    nabo_rhythm_play_start(&s_game);
    ESP_LOGI(TAG, "start %s %s, %u BPM, %u notes", rh_mode_name(s_game.mode),
             rh_roman[s_game.level], s_game.bpm, s_game.notes);
}

static void finish_level(void) {
    nabo_rhythm_play_stop();
    int pct = nabo_rhythm_percent(&s_game);
    char grade = nabo_rhythm_grade(pct);
    s_passed = pct >= 70;
    int index = s_game.mode * RH_LEVELS + s_game.level;
    bool record = pct + 1 > s_best[index];
    if (record) {
        s_best[index] = (uint8_t)(pct + 1);
        save_best();
    }
    nabo_rhythm_play_sound(s_passed ? RH_SND_CLEAR : RH_SND_FAIL);
    uint32_t color = rh_mode_color(s_game.mode);
    char text[48];
    snprintf(text, sizeof(text), "%s %s", rh_mode_name(s_game.mode), rh_roman[s_game.level]);
    rh_set_text(s_res_tag, text);
    lv_obj_set_style_bg_color(s_res_tag, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(s_res_box, lv_color_hex(s_passed ? color : RH_INK), 0);
    snprintf(text, sizeof(text), "%c", grade);
    rh_set_text(s_res_grade, text);
    lv_obj_center(s_res_grade);
    rh_set_text(s_res_title, s_passed ? "CLEAR!" : "AGAIN");
    snprintf(text, sizeof(text), "%d%%", pct);
    rh_set_text(s_res_pct, text);
    lv_obj_set_style_text_color(s_res_pct, lv_color_hex(color), 0);
    unsigned counts[4] = {s_game.perfect, s_game.good, s_game.miss, s_game.extra};
    for (int i = 0; i < 4; ++i) {
        unsigned width = s_game.notes ? counts[i] * 120u / s_game.notes : 0;
        lv_obj_set_width(s_res_bars[i], (int32_t)(width > 120 ? 120 : width));
        lv_obj_set_style_bg_color(s_res_bars[i], lv_color_hex(i == 0 ? color : RH_INK), 0);
        snprintf(text, sizeof(text), "%u", counts[i]);
        rh_set_text(s_res_counts[i], text);
    }
    const char* note = grade == 'S'   ? "完美复刻！节奏感满分"
                       : grade == 'A' ? "很稳！再冲一次 S"
                       : grade == 'B' ? "过关！节奏不错"
                       : grade == 'C' ? "差一点，听准切口再来"
                                      : "多听几遍示范再拍";
    if (record && s_best[index] > 1)
        snprintf(text, sizeof(text), "%s · 新纪录", note);
    else
        snprintf(text, sizeof(text), "%s", note);
    rh_set_text(s_res_note, text);
    rh_set_text(s_res_hint, s_passed ? "OK 下一关 · 下键 重来\n上键 选关" : "OK 再来一次\n上键 选关");
    show_view(VIEW_RESULT);
    ESP_LOGI(TAG, "result %d%% %c perfect=%u good=%u miss=%u extra=%u", pct, grade, s_game.perfect,
             s_game.good, s_game.miss, s_game.extra);
}

bool nabo_rhythm_ui_open(lv_obj_t* screen, const lv_font_t* cjk_font) {
    if (s_root)
        return true;
    rh_cjk = cjk_font;
    load_best();
    s_root = rh_box(screen, 0, 0, 240, 320, RH_PAPER);
    if (!s_root)
        return false;
    build_grid(s_root);
    build_select();
    s_play = layer();
    rh_stage_build(s_play);
    build_result();
    lv_obj_move_foreground(s_root);
    show_view(VIEW_SELECT);
    show_select();
    return true;
}

void nabo_rhythm_ui_close(void) {
    nabo_rhythm_play_stop();
    if (!s_root)
        return;
    rh_stage_forget();
    lv_obj_delete(s_root);
    s_root = NULL;
}

bool nabo_rhythm_ui_in_song(void) { return s_root && s_view == VIEW_PLAY; }

void nabo_rhythm_ui_reset_best(void) {
    memset(s_best, 0, sizeof(s_best));
    save_best();
    if (s_root && s_view == VIEW_SELECT)
        show_select();
}

void nabo_rhythm_ui_tick(void) {
    if (s_root && s_view == VIEW_PLAY && rh_stage_tick(&s_game))
        finish_level();
}

static void back_to_select(void) {
    nabo_rhythm_play_stop();
    show_view(VIEW_SELECT);
    show_select();
}

bool nabo_rhythm_ui_key(muse_menu_key_t key) {
    if (!s_root)
        return false;
    if (key == MUSE_MENU_LEFT) {
        if (s_view == VIEW_SELECT)
            return false;
        back_to_select();
        return true;
    }
    if (s_view == VIEW_SELECT) {
        if (key == MUSE_MENU_UP || key == MUSE_MENU_DOWN) {
            s_cursor = (s_cursor + (key == MUSE_MENU_UP ? LEVEL_COUNT - 1 : 1)) % LEVEL_COUNT;
            nabo_rhythm_play_sound(RH_SND_MOVE);
            show_select();
        } else if (key == MUSE_MENU_SELECT) {
            start_level(s_cursor);
        }
    } else if (s_view == VIEW_PLAY) {
        if (key == MUSE_MENU_UP)
            back_to_select();
        /* OK and DOWN clicks were already scored on press-down. */
    } else {
        if (key == MUSE_MENU_UP)
            back_to_select();
        else if (key == MUSE_MENU_DOWN)
            start_level(s_cursor);
        else if (key == MUSE_MENU_SELECT)
            start_level(s_passed ? (s_cursor + 1) % LEVEL_COUNT : s_cursor);
    }
    return true;
}

void nabo_rhythm_ui_press(int lane, uint32_t event_ms) {
    if (!s_root || s_view != VIEW_PLAY)
        return;
    int64_t now = esp_timer_get_time();
    uint32_t age_ms = (uint32_t)(now / 1000) - event_ms;
    if (age_ms > 1000)
        age_ms = 0;
    int64_t sample;
    if (!nabo_rhythm_play_sample_at(now - (int64_t)age_ms * 1000, &sample))
        return;
    rh_stage_press(&s_game, lane, sample);
}
