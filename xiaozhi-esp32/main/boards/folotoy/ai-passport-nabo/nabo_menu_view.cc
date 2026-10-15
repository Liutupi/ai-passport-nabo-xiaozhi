#include "nabo_menu_view.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" {
LV_FONT_DECLARE(nabo_menu_font_16);
LV_FONT_DECLARE(nabo_ui_font_18);
LV_FONT_DECLARE(nabo_mono_font_12);
LV_FONT_DECLARE(nabo_poster_font_28);
LV_FONT_DECLARE(nabo_poster_font_48);
}

namespace menu_view {
namespace {

enum Part : uint32_t {
    kRowsPart = 1,
    kInfoPart = 2,
    kCardPart = 4,
    kBodyPart = 8,
    kBigPart = 16,
    kMeterPart = 32,
};

lv_obj_t *panel, *counter, *title, *dot, *subtitle, *footer, *footer_rule;
lv_obj_t *selection, *shadow, *scroll_track, *scroll_thumb;
lv_obj_t *indexes[kSlots], *labels[kSlots], *values[kSlots], *dividers[kSlots];
lv_obj_t *card, *card_shadow, *card_title, *card_sub, *body, *big, *meter, *meter_fill;
uint32_t used, accent = kOrange;
int slots_shown;

uint32_t on(uint32_t color) { return color == kYellow ? kInk : kWhite; }

lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color) {
    auto obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj,
                       static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return obj;
}

lv_obj_t* text(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int x, int y, int w = 0) {
    auto obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_pos(obj, x, y);
    if (w) {
        lv_obj_set_width(obj, w);
        lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    }
    lv_label_set_text(obj, "");
    return obj;
}

// Change-only setters: every LVGL style write invalidates its area.
void show(lv_obj_t* obj, bool visible) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == visible)
        lv_obj_set_flag(obj, LV_OBJ_FLAG_HIDDEN, !visible);
}
void set_text(lv_obj_t* obj, const char* value) {
    if (strcmp(lv_label_get_text(obj), value))
        lv_label_set_text(obj, value);
}
void set_x(lv_obj_t* obj, int x) {
    if (lv_obj_get_style_x(obj, LV_PART_MAIN) != x)
        lv_obj_set_x(obj, x);
}
void set_y(lv_obj_t* obj, int y) {
    if (lv_obj_get_style_y(obj, LV_PART_MAIN) != y)
        lv_obj_set_y(obj, y);
}
void set_w(lv_obj_t* obj, int w) {
    if (lv_obj_get_style_width(obj, LV_PART_MAIN) != w)
        lv_obj_set_width(obj, w);
}
void set_h(lv_obj_t* obj, int h) {
    if (lv_obj_get_style_height(obj, LV_PART_MAIN) != h)
        lv_obj_set_height(obj, h);
}
void set_color(lv_obj_t* obj, uint32_t color) {
    if (lv_color_to_u32(lv_obj_get_style_text_color(obj, LV_PART_MAIN)) !=
        lv_color_to_u32(lv_color_hex(color)))
        lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}
void set_bg(lv_obj_t* obj, uint32_t color) {
    if (lv_color_to_u32(lv_obj_get_style_bg_color(obj, LV_PART_MAIN)) !=
        lv_color_to_u32(lv_color_hex(color)))
        lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
}

void hide_slots(int from) {
    for (int i = from; i < kSlots; ++i) {
        show(indexes[i], false);
        show(labels[i], false);
        show(values[i], false);
        show(dividers[i], false);
    }
}

}  // namespace

void Build(lv_obj_t* screen) {
    if (panel)
        return;
    panel = box(screen, 0, 0, 240, 320, kPaper);
    // Faint 24 px poster grid, same as ECHO CLUB.
    for (int x = 12; x < 240; x += 24)
        box(panel, x, 0, 1, 320, kGrid);
    for (int y = 4; y < 320; y += 24)
        box(panel, 0, y, 240, 1, kGrid);
    auto chip = text(panel, &nabo_mono_font_12, kWhite, 12, 7);
    lv_obj_set_style_bg_color(chip, lv_color_hex(kInk), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(chip, 5, 0);
    lv_obj_set_style_pad_ver(chip, 2, 0);
    lv_label_set_text(chip, "NABO PASSPORT");
    counter = text(panel, &nabo_mono_font_12, kMuted, 0, 12);
    lv_obj_set_width(counter, 80);
    lv_obj_set_style_text_align(counter, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(counter, LV_ALIGN_TOP_RIGHT, -12, 9);
    title = text(panel, &nabo_poster_font_28, kInk, 12, 30);
    dot = box(panel, 0, 52, 8, 8, kOrange);
    subtitle = text(panel, &nabo_menu_font_16, kMuted, 12, 64, 216);
    box(panel, 12, 86, 216, 2, kInk);

    shadow = box(panel, 15, 0, 213, 28, kInk);
    selection = box(panel, 12, 0, 213, 28, kOrange);
    for (int i = 0; i < kSlots; ++i) {
        dividers[i] = box(panel, 12, 0, 216, 1, kGrid);
        indexes[i] = text(panel, &nabo_mono_font_12, kMuted, 20, 0);
        labels[i] = text(panel, &nabo_ui_font_18, kInk, 46, 0, 124);
        values[i] = text(panel, &nabo_ui_font_18, kInk, 120, 0, 98);
        lv_obj_set_style_text_align(values[i], LV_TEXT_ALIGN_RIGHT, 0);
    }
    scroll_track = box(panel, 231, 0, 2, 10, kGrid);
    scroll_thumb = box(panel, 231, 0, 2, 10, kInk);

    card_shadow = box(panel, 15, 95, 213, 54, kInk);
    card = box(panel, 12, 92, 213, 54, kPink);
    card_title = text(card, &nabo_ui_font_18, kWhite, 12, 6, 190);
    card_sub = text(card, &nabo_menu_font_16, kWhite, 12, 30, 190);
    body = text(panel, &nabo_menu_font_16, kInk, 12, 96);
    lv_obj_set_width(body, 216);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(body, 6, 0);
    big = text(panel, &nabo_poster_font_48, kInk, 12, 96);
    meter = box(panel, 12, 170, 216, 22, kPaper);
    lv_obj_set_style_border_width(meter, 2, 0);
    lv_obj_set_style_border_color(meter, lv_color_hex(kInk), 0);
    meter_fill = box(meter, 2, 2, 0, 18, kOrange);

    footer_rule = box(panel, 12, 286, 216, 1, kInk);
    footer = text(panel, &nabo_menu_font_16, kMuted, 12, 292);
    lv_obj_set_width(footer, 220);
    lv_label_set_long_mode(footer, LV_LABEL_LONG_WRAP);
    used = 0;
    slots_shown = 0;
}

void Free() {
    if (!panel)
        return;
    lv_obj_delete(panel);
    panel = nullptr;
}

bool Built() { return panel != nullptr; }

void Begin() { used = 0; }

void End() {
    if (!(used & (kRowsPart | kInfoPart)))
        hide_slots(0);
    bool rows = used & kRowsPart;
    if (!rows) {
        show(selection, false);
        show(shadow, false);
        show(scroll_track, false);
        show(scroll_thumb, false);
    }
    show(card, used & kCardPart);
    show(card_shadow, used & kCardPart);
    show(body, used & kBodyPart);
    show(big, used & kBigPart);
    show(meter, used & kMeterPart);
}

void Header(const char* title_text, const char* subtitle_text, uint32_t color,
            const char* counter_text) {
    accent = color;
    set_text(subtitle, subtitle_text);
    set_text(counter, counter_text ? counter_text : "");
    set_bg(dot, color);
    if (strcmp(lv_label_get_text(title), title_text)) {
        lv_label_set_text(title, title_text);
        lv_point_t size;
        lv_text_get_size(&size, title_text, &nabo_poster_font_28, 0, 0, LV_COORD_MAX,
                         LV_TEXT_FLAG_NONE);
        lv_obj_set_x(dot, 12 + size.x + 3);
    }
}

int First(int cursor, int count, int visible) {
    int first = cursor > visible - 2 ? cursor - (visible - 2) : 0;
    if (first + visible > count)
        first = std::max(0, count - visible);
    return first;
}

void Rows(const char* const* names, int count, int cursor, bool editing, int top, int visible) {
    used |= kRowsPart;
    int first = First(cursor, count, visible);
    uint32_t block = editing ? kInk : accent;
    uint32_t ink_on = editing ? kWhite : on(accent);
    int selected_y = top + (cursor - first) * kRowPitch - 4;
    set_y(selection, selected_y);
    set_y(shadow, selected_y + 3);
    set_bg(selection, block);
    show(selection, true);
    show(shadow, true);
    char index[16];
    for (int i = 0; i < kSlots; ++i) {
        int item = first + i;
        if (i >= visible || item >= count) {
            hide_slots(i);
            break;
        }
        int y = top + i * kRowPitch;
        bool on_row = item == cursor;
        show(dividers[i], false);
        show(indexes[i], true);
        show(labels[i], true);
        show(values[i], true);
        set_y(indexes[i], y + 5);
        set_y(labels[i], y);
        set_y(values[i], y);
        set_x(labels[i], 46);
        set_w(labels[i], 124);
        set_x(values[i], 120);
        set_w(values[i], 98);
        snprintf(index, sizeof(index), "%02d", item);
        set_text(indexes[i], index);
        set_text(labels[i], names[item]);
        set_text(values[i], "");
        set_color(indexes[i], on_row ? ink_on : kMuted);
        set_color(labels[i], on_row ? ink_on : kInk);
        set_color(values[i], on_row ? (editing ? kYellow : ink_on) : 0x4A463F);
    }
    slots_shown = std::min(visible, count - first);
    bool scroll = count > visible;
    show(scroll_track, scroll);
    show(scroll_thumb, scroll);
    if (scroll) {
        int track = visible * kRowPitch - 6;
        int thumb = std::max(18, track * visible / count);
        set_y(scroll_track, top - 3);
        set_h(scroll_track, track);
        set_h(scroll_thumb, thumb);
        set_y(scroll_thumb, top - 3 + (track - thumb) * first / (count - visible));
    }
}

void Value(int slot, const char* value) {
    if (slot >= 0 && slot < slots_shown)
        set_text(values[slot], value);
}

void Info(const char* const* names, const char* const* texts, int count, int top) {
    used |= kInfoPart;
    count = std::min(count, kSlots);
    for (int i = 0; i < count; ++i) {
        int y = top + i * kRowPitch;
        show(indexes[i], false);
        show(labels[i], true);
        show(values[i], true);
        show(dividers[i], true);
        set_y(dividers[i], y + kRowPitch - 6);
        set_y(labels[i], y);
        set_y(values[i], y);
        set_x(labels[i], 12);
        set_w(labels[i], 80);
        set_x(values[i], 84);
        set_w(values[i], 144);
        set_text(labels[i], names[i]);
        set_text(values[i], texts[i]);
        set_color(labels[i], kMuted);
        set_color(values[i], kInk);
    }
    hide_slots(count);
    slots_shown = count;
}

void Card(const char* card_title_text, const char* card_sub_text) {
    used |= kCardPart;
    set_bg(card, accent);
    set_color(card_title, on(accent));
    set_color(card_sub, on(accent));
    set_text(card_title, card_title_text);
    set_text(card_sub, card_sub_text);
}

void Body(const char* value, int y) {
    used |= kBodyPart;
    set_y(body, y);
    set_text(body, value);
}

void Big(const char* value, int y) {
    used |= kBigPart;
    set_y(big, y);
    set_text(big, value);
}

void Meter(int percent, int y) {
    used |= kMeterPart;
    set_y(meter, y);
    set_w(meter_fill, 212 * std::clamp(percent, 0, 100) / 100);
    set_bg(meter_fill, percent <= 20 ? kOrange : accent);
}

void Footer(const char* value) {
    set_text(footer, value);
    int lines = 1;
    for (const char* p = value; *p; ++p)
        lines += *p == '\n';
    int y = 320 - 6 - lines * 20;
    set_y(footer, y);
    set_y(footer_rule, y - 6);
}

}  // namespace menu_view
