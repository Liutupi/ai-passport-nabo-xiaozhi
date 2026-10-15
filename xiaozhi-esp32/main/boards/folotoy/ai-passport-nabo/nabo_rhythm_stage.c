/* Play screen: CALL track (demonstration), YOU track (response), playhead,
 * beat dots and hit bursts. Only moved objects are touched each frame so the
 * partial LVGL refresh stays small on the C3. */
#include <stdio.h>
#include "esp_timer.h"
#include "nabo_rhythm_play.h"
#include "nabo_rhythm_style.h"

#define TRACK_X 52
#define TRACK_W 176
#define SLOT (TRACK_W / RH_STEPS)
#define CALL_Y 98
#define YOU_Y 144
#define BAR_H 24
#define TAIL_SAMPLES (RH_RATE / 2)

static lv_obj_t *s_level, *s_bpm, *s_counter, *s_head, *s_sub, *s_you_label;
static lv_obj_t *s_call_bar, *s_cuts[RH_STEPS], *s_you_frame, *s_you_fill;
static lv_obj_t *s_ghosts[RH_STEPS], *s_marks[RH_STEPS], *s_misses[RH_STEPS], *s_playhead;
static lv_obj_t *s_dots[4], *s_ring, *s_judge, *s_pills[RH_PHRASES], *s_stats, *s_hint;
static int s_bar = -2, s_stage = -1, s_mode;
static uint8_t s_shown_judge[RH_STEPS];
static int64_t s_judge_us;
static uint32_t s_judge_color;
static uint16_t s_shown_stats = 0xFFFF;

/* Setters that skip unchanged values: LVGL invalidates on every style write. */
static void hide(lv_obj_t* obj, bool hidden) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != hidden)
        lv_obj_set_flag(obj, LV_OBJ_FLAG_HIDDEN, hidden);
}
static void move_x(lv_obj_t* obj, int x) {
    if (lv_obj_get_style_x(obj, 0) != x)
        lv_obj_set_x(obj, x);
}
static void set_width(lv_obj_t* obj, int width) {
    if (lv_obj_get_style_width(obj, 0) != width)
        lv_obj_set_width(obj, width);
}
static void paint(lv_obj_t* obj, uint32_t color) {
    if (lv_color_to_u32(lv_obj_get_style_bg_color(obj, 0)) != lv_color_to_u32(lv_color_hex(color)))
        lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
}

void rh_stage_build(lv_obj_t* parent) {
    s_level = rh_chip(parent, "ECHO I", RH_INK, RH_WHITE, 12, 10);
    s_bpm = rh_chip(parent, "90 BPM", RH_ORANGE, RH_WHITE, 84, 10);
    s_counter = rh_text(parent, &nabo_mono_font_12, RH_INK, "1/4", 0, 12);
    lv_obj_align(s_counter, LV_ALIGN_TOP_RIGHT, -12, 12);
    s_head = rh_text(parent, &nabo_poster_font_28, RH_INK, "READY", 12, 30);
    lv_obj_set_style_pad_hor(s_head, 0, 0);
    s_sub = rh_text(parent, rh_cjk, RH_INK, "", 12, 68);

    rh_text(parent, &nabo_mono_font_12, RH_INK, "CALL", 12, CALL_Y + 5);
    s_call_bar = rh_box(parent, TRACK_X, CALL_Y, TRACK_W, BAR_H, RH_INK);
    for (int s = 0; s < RH_STEPS; ++s) {
        s_cuts[s] = rh_box(parent, TRACK_X + s * SLOT, CALL_Y, 4, BAR_H, RH_PAPER);
        lv_obj_t* tick = rh_box(parent, TRACK_X + s * SLOT, CALL_Y + BAR_H + 4, 2, s & 1 ? 3 : 7,
                                s & 1 ? RH_MUTED : RH_INK);
        (void)tick;
    }
    s_you_label = rh_text(parent, &nabo_mono_font_12, RH_ORANGE, "YOU", 12, YOU_Y + 5);
    s_you_fill = rh_box(parent, TRACK_X, YOU_Y, 0, BAR_H, RH_ORANGE);
    s_you_frame = rh_box(parent, TRACK_X, YOU_Y, TRACK_W, BAR_H, RH_ORANGE);
    lv_obj_set_style_bg_opa(s_you_frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_you_frame, 2, 0);
    lv_obj_set_style_border_color(s_you_frame, lv_color_hex(RH_ORANGE), 0);
    for (int s = 0; s < RH_STEPS; ++s) {
        s_ghosts[s] = rh_box(parent, TRACK_X + s * SLOT, YOU_Y + BAR_H + 4, 6, 6, RH_MUTED);
        lv_obj_set_style_radius(s_ghosts[s], 3, 0);
        s_marks[s] = rh_box(parent, TRACK_X + s * SLOT, YOU_Y - 3, 4, BAR_H + 6, RH_INK);
        s_misses[s] = rh_text(parent, &lv_font_montserrat_14, RH_ORANGE, LV_SYMBOL_CLOSE,
                              TRACK_X + s * SLOT - 4, YOU_Y + BAR_H + 2);
    }
    s_playhead = rh_box(parent, TRACK_X, CALL_Y - 6, 2, BAR_H + 12, RH_INK);

    for (int i = 0; i < 4; ++i) {
        s_dots[i] = rh_box(parent, 78 + i * 24, 194, 12, 12, RH_PAPER);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_dots[i], 2, 0);
        lv_obj_set_style_border_color(s_dots[i], lv_color_hex(RH_INK), 0);
    }
    s_ring = rh_box(parent, 120, 238, 10, 10, RH_PAPER);
    lv_obj_set_style_bg_opa(s_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ring, 4, 0);
    s_judge = rh_text(parent, &nabo_poster_font_28, RH_INK, "", 0, 222);
    lv_obj_set_width(s_judge, 240);
    lv_obj_set_style_text_align(s_judge, LV_TEXT_ALIGN_CENTER, 0);
    for (int i = 0; i < RH_PHRASES; ++i)
        s_pills[i] = rh_box(parent, 12 + i * 55, 270, 51, 6, RH_GRID);
    s_stats = rh_text(parent, &nabo_mono_font_12, RH_INK, "", 12, 282);
    rh_box(parent, 12, 297, 216, 1, RH_INK);
    s_hint = rh_text(parent, rh_cjk, RH_MUTED, "单击上键 退出本关", 12, 300);
}

void rh_stage_forget(void) {
    s_level = NULL;
    s_bar = -2;
}

static void set_head(int stage) {
    if (stage == s_stage)
        return;
    s_stage = stage;
    bool turn = stage == RH_STAGE_TURN;
    rh_set_text(s_head, stage == RH_STAGE_COUNTIN  ? "READY"
                        : stage == RH_STAGE_LISTEN ? "LISTEN."
                        : turn                     ? "YOUR TURN"
                                                   : "DONE");
    lv_obj_set_style_bg_opa(s_head, turn ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(s_head, lv_color_hex(rh_mode_color(s_mode)), 0);
    lv_obj_set_style_text_color(s_head, lv_color_hex(turn ? RH_WHITE : RH_INK), 0);
    lv_obj_set_style_pad_hor(s_head, turn ? 6 : 0, 0);
}

void rh_stage_begin(const nabo_rhythm_t* game) {
    char text[32];
    s_mode = game->mode;
    uint32_t color = rh_mode_color(game->mode);
    snprintf(text, sizeof(text), "%s %s", rh_mode_name(game->mode), rh_roman[game->level]);
    rh_set_text(s_level, text);
    snprintf(text, sizeof(text), "%u BPM", game->bpm);
    rh_set_text(s_bpm, text);
    lv_obj_update_layout(s_level);
    lv_obj_set_x(s_bpm, 12 + lv_obj_get_width(s_level) + 6);
    lv_obj_set_style_bg_color(s_bpm, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(s_you_frame, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(s_you_fill, lv_color_hex(color), 0);
    lv_obj_set_style_text_color(s_you_label, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(s_ring, lv_color_hex(color), 0);
    s_bar = -2;
    s_stage = -1;
    s_judge_us = 0;
    s_shown_stats = 0xFFFF;
    set_head(RH_STAGE_COUNTIN);
    rh_set_text(s_sub, "跟着节拍，先听示范");
    rh_set_text(s_judge, "");
    hide(s_ring, true);
    hide(s_playhead, true);
    for (int s = 0; s < RH_STEPS; ++s) {
        hide(s_cuts[s], true);
        hide(s_ghosts[s], true);
        hide(s_marks[s], true);
        hide(s_misses[s], true);
        s_shown_judge[s] = 0xFF;
    }
    lv_obj_set_width(s_you_fill, 0);
    for (int i = 0; i < RH_PHRASES; ++i)
        lv_obj_set_style_bg_color(s_pills[i], lv_color_hex(RH_GRID), 0);
    rh_set_text(s_hint, game->mode == RH_MODE_ECHO ? "OK 或下键拍 · 上键退出"
                                                   : "OK 低音 · 下键高音 · 上退出");
}

static void feedback(const char* label, uint32_t color, bool ring) {
    rh_set_text(s_judge, label);
    lv_obj_set_style_text_color(s_judge, lv_color_hex(color), 0);
    s_judge_us = esp_timer_get_time();
    s_judge_color = color;
    hide(s_ring, !ring);
}

static int x_for(const nabo_rhythm_t* game, int bar, uint32_t sample) {
    uint32_t start = nabo_rhythm_step_sample(game, bar * RH_STEPS);
    uint32_t length = nabo_rhythm_step_sample(game, (bar + 1) * RH_STEPS) - start;
    int64_t into = (int64_t)sample - start;
    int x = TRACK_X + (int)(into * TRACK_W / (int64_t)length);
    return x < TRACK_X ? TRACK_X : x > TRACK_X + TRACK_W - 4 ? TRACK_X + TRACK_W - 4 : x;
}

static void layout_bar(const nabo_rhythm_t* game, const rh_position_t* pos) {
    int phrase = pos->phrase < 0 ? 0 : pos->phrase;
    bool duet = game->two_keys[phrase];
    for (int s = 0; s < RH_STEPS; ++s) {
        bool note = (game->mask[phrase] >> s) & 1, high = (game->high[phrase] >> s) & 1;
        lv_obj_set_style_bg_color(s_cuts[s], lv_color_hex(high ? RH_BLUE : RH_PAPER), 0);
        lv_obj_set_y(s_cuts[s], high ? CALL_Y - 6 : CALL_Y);
        lv_obj_set_height(s_cuts[s], high ? BAR_H + 6 : BAR_H);
        hide(s_cuts[s], pos->stage != RH_STAGE_TURN || !note);
        hide(s_ghosts[s], pos->stage != RH_STAGE_TURN || !note);
        lv_obj_set_style_bg_color(s_ghosts[s], lv_color_hex(high ? RH_BLUE : RH_MUTED), 0);
        hide(s_marks[s], true);
        hide(s_misses[s], true);
        s_shown_judge[s] = 0xFF;
    }
    lv_obj_set_width(s_you_fill, 0);
    hide(s_playhead, pos->stage == RH_STAGE_COUNTIN || pos->stage == RH_STAGE_DONE);
    lv_obj_set_y(s_playhead, pos->stage == RH_STAGE_TURN ? YOU_Y - 6 : CALL_Y - 6);
    for (int i = 0; i < RH_PHRASES; ++i) {
        uint32_t color = i < pos->phrase                                  ? RH_INK
                         : i == pos->phrase && pos->stage != RH_STAGE_DONE ? rh_mode_color(s_mode)
                                                                           : RH_GRID;
        if (pos->stage == RH_STAGE_DONE)
            color = RH_INK;
        lv_obj_set_style_bg_color(s_pills[i], lv_color_hex(color), 0);
    }
    char text[24];
    snprintf(text, sizeof(text), "%d/%d", phrase + 1, RH_PHRASES);
    rh_set_text(s_counter, text);
    set_head(pos->stage);
    if (pos->stage == RH_STAGE_LISTEN)
        rh_set_text(s_sub, duet ? "蓝色切口是高音，听准" : "先听：黑条切口就是拍点");
    else if (pos->stage == RH_STAGE_TURN)
        rh_set_text(s_sub, duet ? "OK 拍低音 · 下键拍高音" : "轮到你：在切口处拍回去");
}

static void refresh_marks(const nabo_rhythm_t* game, const rh_position_t* pos) {
    if (pos->stage != RH_STAGE_TURN)
        return;
    int p = pos->phrase;
    for (int s = 0; s < RH_STEPS; ++s) {
        uint8_t judge = game->judge[p][s];
        if (judge == s_shown_judge[s])
            continue;
        s_shown_judge[s] = judge;
        bool hit = judge == RH_JUDGE_PERFECT || judge == RH_JUDGE_GOOD;
        hide(s_marks[s], !hit);
        hide(s_misses[s], judge != RH_JUDGE_MISS);
        if (judge != RH_JUDGE_NONE)
            hide(s_ghosts[s], true);
        if (hit) {
            bool high = (game->high[p] >> s) & 1;
            lv_obj_set_x(s_marks[s], x_for(game, pos->bar, game->tap_at[p][s]));
            lv_obj_set_style_bg_color(s_marks[s], lv_color_hex(high ? RH_BLUE : RH_INK), 0);
            lv_obj_set_style_bg_opa(s_marks[s], judge == RH_JUDGE_PERFECT ? LV_OPA_COVER : LV_OPA_60, 0);
        }
    }
}

static void animate_feedback(void) {
    if (!s_judge_us)
        return;
    int64_t age = (esp_timer_get_time() - s_judge_us) / 1000;
    if (age > 520) {
        rh_set_text(s_judge, "");
        hide(s_ring, true);
        s_judge_us = 0;
        return;
    }
    if (!lv_obj_has_flag(s_ring, LV_OBJ_FLAG_HIDDEN)) {
        int size = 34 + (int)age * 3 / 10;
        lv_obj_set_size(s_ring, size, size);
        lv_obj_set_pos(s_ring, 120 - size / 2, 238 - size / 2);
        int opa = age >= 320 ? 0 : 255 - (int)age * 255 / 320;
        lv_obj_set_style_border_opa(s_ring, (lv_opa_t)opa, 0);
        lv_obj_set_style_border_color(s_ring, lv_color_hex(s_judge_color), 0);
    }
}

bool rh_stage_tick(nabo_rhythm_t* game) {
    if (!s_level)
        return false;
    animate_feedback();
    int64_t sample;
    if (!nabo_rhythm_play_sample_at(esp_timer_get_time(), &sample))
        return false;
    if (sample < 0)
        sample = 0;
    rh_position_t pos;
    nabo_rhythm_locate(game, (uint32_t)sample, &pos);
    if (nabo_rhythm_update(game, (uint32_t)sample)) {
        nabo_rhythm_play_sound(RH_SND_MISS);
        feedback("MISS", RH_MUTED, false);
    }
    if (pos.bar != s_bar) {
        s_bar = pos.bar;
        layout_bar(game, &pos);
    }
    if (pos.stage == RH_STAGE_LISTEN) {
        for (int s = 0; s < RH_STEPS; ++s) {
            bool note = (game->mask[pos.phrase] >> s) & 1;
            /* Reveal each cut as its demonstration note sounds. */
            hide(s_cuts[s], !note || s * 1000 / RH_STEPS > pos.bar_permille);
        }
    }
    if (pos.stage == RH_STAGE_LISTEN || pos.stage == RH_STAGE_TURN)
        move_x(s_playhead, TRACK_X + pos.bar_permille * TRACK_W / 1000);
    if (pos.stage == RH_STAGE_TURN)
        set_width(s_you_fill, pos.bar_permille * TRACK_W / 1000);
    refresh_marks(game, &pos);
    for (int i = 0; i < 4; ++i) {
        bool on = pos.stage != RH_STAGE_DONE && i <= pos.beat;
        uint32_t color = !on                            ? RH_PAPER
                         : pos.stage == RH_STAGE_TURN ? rh_mode_color(s_mode)
                                                      : RH_INK;
        paint(s_dots[i], color);
    }
    uint16_t stats = (uint16_t)(game->perfect * 7 + game->good * 131 + game->miss * 1543 + game->extra);
    if (stats != s_shown_stats) {
        s_shown_stats = stats;
        char text[48];
        snprintf(text, sizeof(text), "PERFECT %u  GOOD %u  MISS %u", game->perfect, game->good,
                 game->miss);
        rh_set_text(s_stats, text);
    }
    return pos.stage == RH_STAGE_DONE &&
           (uint32_t)sample >= nabo_rhythm_length(game) + TAIL_SAMPLES;
}

void rh_stage_press(nabo_rhythm_t* game, int lane, int64_t sample) {
    if (sample < 0)
        sample = 0;
    rh_tap_t tap = nabo_rhythm_tap(game, (uint32_t)sample, lane);
    nabo_rhythm_play_sound(lane ? RH_SND_TAP_HIGH : RH_SND_TAP_LOW);
    if (tap.judge == RH_JUDGE_PERFECT) {
        nabo_rhythm_play_sound(RH_SND_PERFECT);
        feedback("PERFECT", rh_mode_color(game->mode), true);
    } else if (tap.judge == RH_JUDGE_GOOD) {
        feedback(tap.offset_ms < 0 ? "EARLY" : "LATE", RH_INK, true);
    } else if (tap.wrong_key) {
        feedback("SWAP!", RH_BLUE, false);
    }
}
