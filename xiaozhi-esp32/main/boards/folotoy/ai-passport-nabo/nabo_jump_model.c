/* Original low-memory isometric jump game, inspired by the supplied screenshot.
 * No WeChat assets, network, framebuffer, heap or ESP-IDF dependency. */
#include "nabo_jump_model.h"
#include <stdlib.h>
#include <string.h>
#include "nabo_jump_assets.h"
#include "nabo_jump_rig.h"

static uint32_t random_next(nabo_jump_t* g) {
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g->rng = x ? x : 0x98765123;
}
static int clamp(int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; }
static int ease(int t, int duration);
/* Sizes: full 96 px sprite, the same sprite at 72 %, and a cup whose rim
 * (14 x 7 px) just fits NABO's 12 x 5 px footprint. */
static const int kTolerance[JUMP_SIZE_COUNT] = {16, 11, 9};
static const int kCenter[JUMP_SIZE_COUNT] = {5, 4, 3};
#define SMALL_SCALE 184 /* /256 */
int nabo_jump_tolerance(int size) { return kTolerance[clamp(size, 0, JUMP_SIZE_COUNT - 1)]; }
int nabo_jump_center_tolerance(int size) { return kCenter[clamp(size, 0, JUMP_SIZE_COUNT - 1)]; }
void nabo_jump_difficulty(int steps, int* gap_min, int* gap_max, int* small_pct, int* cup_pct) {
    // The first platforms stay as before; then gaps widen and platforms
    // shrink, and from about 20 landings single-person coffee cups appear.
    *gap_min = 65 + clamp((steps - 15) / 3, 0, 8);
    *gap_max = 94 + clamp((steps - 8) / 2, 0, 16);
    *small_pct = clamp((steps - 6) * 3, 0, 35);
    *cup_pct = clamp((steps - 18) * 3, 0, 45);
}
static void plan_future(nabo_jump_t* g) {
    int gap_min, gap_max, small_pct, cup_pct;
    nabo_jump_difficulty(g->steps, &gap_min, &gap_max, &small_pct, &cup_pct);
    g->future_skin = (int)(random_next(g) % 6);
    g->future_dir = (random_next(g) & 1) ? 1 : -1;
    g->future_gap = gap_min + (int)(random_next(g) % (unsigned)(gap_max - gap_min + 1));
    g->future_size = JUMP_SIZE_NORMAL;
    // Early rounds keep the original random sequence.
    if (small_pct + cup_pct) {
        int roll = (int)(random_next(g) % 100);
        g->future_size = roll < cup_pct                ? JUMP_SIZE_CUP
                         : roll < cup_pct + small_pct ? JUMP_SIZE_SMALL
                                                      : JUMP_SIZE_NORMAL;
    }
}
static int scroll_settle(const nabo_jump_t* g) {
    if (!g->pending_charge)
        return 240;
    // If the player starts the next charge early, pan as soon as the first
    // contact has been absorbed. Never jump the camera when input arrives.
    return clamp((int32_t)(g->pending_at - g->changed_at), 90, 240);
}
static uint16_t color(unsigned c) {
    return (uint16_t)(((c >> 19) & 31) << 11 | ((c >> 10) & 63) << 5 | ((c >> 3) & 31));
}
static unsigned mix(unsigned a, unsigned b, int t) {
    unsigned r = (((a >> 16) & 255) * (256 - t) + ((b >> 16) & 255) * t) >> 8;
    unsigned g = (((a >> 8) & 255) * (256 - t) + ((b >> 8) & 255) * t) >> 8;
    unsigned q = ((a & 255) * (256 - t) + (b & 255) * t) >> 8;
    return r << 16 | g << 8 | q;
}
void nabo_jump_reset(nabo_jump_t* g, uint32_t seed, uint32_t best) {
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 1;
    g->best = best;
    g->dir = 1;
    g->gap = 82;
    g->next_skin = 1;
    g->phase = JUMP_READY;
}
void nabo_jump_press(nabo_jump_t* g, uint32_t now) {
    if (g->phase == JUMP_OVER) {
        int character = g->character;
        nabo_jump_reset(g, random_next(g), g->best);
        g->character = character;
    }
    if (g->phase == JUMP_SCROLLING || (g->phase == JUMP_FLYING && now - g->changed_at >= 320)) {
        if (!g->pending_charge) {
            g->pending_charge = true;
            g->pending_released = false;
            g->pending_at = now;
            g->pending_hold = 0;
        }
        return;
    }
    if (g->phase != JUMP_READY)
        return;
    g->charge_at = now;
    g->phase = JUMP_CHARGING;
}
void nabo_jump_release(nabo_jump_t* g, uint32_t now) {
    if (g->pending_charge && (g->phase == JUMP_SCROLLING || g->phase == JUMP_FLYING)) {
        if (!g->pending_released) {
            g->pending_hold = (int)(now - g->pending_at);
            g->pending_hold = clamp(g->pending_hold, 0, 1400);
            g->pending_released = true;
        }
        return;
    }
    if (g->phase != JUMP_CHARGING)
        return;
    uint32_t hold = now - g->charge_at;
    if (hold > 1400)
        hold = 1400;
    g->distance = 4 + (int)hold * 150 / 1000;
    g->changed_at = now;
    g->phase = JUMP_FLYING;
    g->landed = false;
    g->centered = false;
}
bool nabo_jump_tick(nabo_jump_t* g, uint32_t now) {
    uint32_t age = now - g->changed_at;
    if (g->phase == JUMP_CHARGING)
        return true;
    if (g->phase == JUMP_FLYING) {
        int t = clamp((int)age, 0, 400);
        g->player_x = g->dir * g->distance * t / 400;
        g->player_y = -g->distance * t / 800;
        g->lift = 4 * 78 * t * (400 - t) / (400 * 400);
        if (age >= 400) {
            int delta = g->distance - g->gap;
            // The diagonal landing axis crosses the rendered top within
            // 16 px on a full platform; smaller tops are tighter.
            if (abs(delta) <= nabo_jump_tolerance(g->next_size)) {
                g->landed = true;
                g->centered = abs(delta) <= nabo_jump_center_tolerance(g->next_size);
                if (g->centered) {
                    if (g->combo < 1000)
                        ++g->combo;
                } else
                    g->combo = 0;
                // A one-person cup is worth an extra point.
                g->score += (g->centered ? 2 * g->combo : 1) + (g->next_size == JUMP_SIZE_CUP);
                if (g->score > g->best)
                    g->best = g->score;
                if (g->steps < 100000)
                    ++g->steps;
                plan_future(g);
                g->sound = true;
                g->phase = JUMP_SCROLLING;
                g->changed_at = now;
            } else if (g->distance <= 16) {
                g->pending_charge = g->pending_released = false;
                g->player_x = g->player_y = g->lift = 0;
                g->phase = JUMP_READY;
            } else {
                g->pending_charge = g->pending_released = false;
                g->phase = JUMP_FALLING;
                g->changed_at = now;
                g->fall_ground_drop = 55 + (g->distance - g->gap) / 2;
                int side = g->dir * (delta >= 0 ? 1 : -1);
                int screen_x = clamp(120 + g->dir * g->gap / 2 + side * 50, 30, 210);
                g->fall_target_x = screen_x - 120 + g->dir * g->gap / 2;
                int velocity = 780 - g->distance * 1000 / 800;
                g->fall_contact_ms = 0;
                while (velocity * g->fall_contact_ms / 1000 +
                           g->fall_contact_ms * g->fall_contact_ms / 6000 <
                       g->fall_ground_drop)
                    g->fall_contact_ms += 5;
            }
        }
        return true;
    }
    if (g->phase == JUMP_SCROLLING) {
        if (age >= (unsigned)scroll_settle(g) + 280) {
            g->skin = g->next_skin;
            g->next_skin = g->future_skin;
            g->size = g->next_size;
            g->next_size = g->future_size;
            g->dir = g->future_dir;
            g->gap = g->future_gap;
            g->player_x = g->player_y = g->lift = 0;
            g->phase = JUMP_READY;
            if (g->pending_charge) {
                uint32_t started = g->pending_at;
                int hold = g->pending_hold;
                bool released = g->pending_released;
                g->pending_charge = g->pending_released = false;
                nabo_jump_press(g, started);
                if (released) {
                    nabo_jump_release(g, started + (uint32_t)hold);
                    // Preserve the input duration, while starting this flight
                    // now rather than skipping ahead to the earlier release.
                    g->changed_at = now;
                    g->charge_at = now - (uint32_t)hold;
                }
            }
        }
        return true;
    }
    if (g->phase == JUMP_FALLING) {
        int t = clamp((int)age, 0, 560);
        // Continue the flight's endpoint velocity. A missed jump must never
        // freeze in mid-air before falling. Horizontal momentum tapers away.
        int slide = ease(t, 160);
        g->player_x =
            g->dir * g->distance + (g->fall_target_x - g->dir * g->distance) * slide / 256;
        int velocity = 780 - g->distance * 1000 / 800;
        g->lift = -clamp(velocity * t / 1000 + t * t / 6000, 0, g->fall_ground_drop);
        if (age >= (unsigned)g->fall_contact_ms + 220) {
            g->phase = JUMP_OVER;
        }
        return true;
    }
    return false;
}
static bool ellipse(int dx, int dy, int rx, int ry) {
    return dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry;
}

/* Blend packed RGB565 directly. Opaque pixels are a single flash read; avoid
 * converting every background pixel through RGB888 on the single-core C3. */
static uint16_t blend565(uint16_t bg, uint16_t fg, unsigned a) {
    if (!a)
        return bg;
    if (a == 255)
        return fg;
    unsigned r = (((bg >> 11) & 31) * (255 - a) + ((fg >> 11) & 31) * a + 127) / 255;
    unsigned g = (((bg >> 5) & 63) * (255 - a) + ((fg >> 5) & 63) * a + 127) / 255;
    unsigned b = ((bg & 31) * (255 - a) + (fg & 31) * a + 127) / 255;
    return (uint16_t)(r << 11 | g << 5 | b);
}
static uint16_t sprite(uint16_t bg, const unsigned char* pixel) {
    return blend565(bg, (uint16_t)(pixel[0] | pixel[1] << 8), pixel[2]);
}
static int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static uint16_t shade(unsigned light, unsigned dark, int dx, int half) {
    return color(mix(light, dark, clamp((dx + half) * 255 / (2 * half), 0, 255)));
}
/* Strict test: integer ellipses with <= grow one-pixel nubs at their poles. */
static bool inside(int dx, int dy, int rx, int ry) {
    return dx * dx * ry * ry + dy * dy * rx * rx < rx * rx * ry * ry;
}
/* One-person items for the late game: three generic coffee cups (no brands),
 * a laptop, a speaker and a vinyl record. Each standing surface is centred on
 * (cx, cy) and about 14 x 7 px, just wider than NABO's 12 x 5 px footprint,
 * so one landing window (nabo_jump_tolerance) fits them all. */
enum { CUP_TAKEAWAY, CUP_LATTE, CUP_ICED, TINY_LAPTOP, TINY_SPEAKER, TINY_RECORD };
static bool cup_shadow(int dx, int dy, uint16_t* c, int floor_y) {
    if (!inside(dx + 6, dy - floor_y, 26, 9))
        return false;
    *c = blend565(*c, color(0x5e637a), inside(dx + 6, dy - floor_y, 19, 6) ? 70 : 40);
    return true;
}
static int cup_half(int dy, int height) { return 14 - clamp(dy, 0, height) * 4 / height; }
static bool takeaway(int dx, int dy, uint16_t* c, unsigned accent) {
    bool hit = cup_shadow(dx, dy, c, 37);
    int half = cup_half(dy, 34);
    if ((dy >= 0 && dy <= 34 && abs(dx) <= half) || inside(dx, dy - 34, 11, 4)) {
        bool sleeve = dy >= 13 && dy <= 25;
        bool seam = dy == 13 || dy == 25;
        *c = sleeve ? shade(seam ? 0x9c7148 : 0xd2a676, seam ? 0x5e4129 : 0x8a6440, dx, half)
                    : shade(0xffffff, 0xc4bdb2, dx, half);
        if (!sleeve && dy >= 28 && dy <= 29)  // printed accent ring near the base
            *c = shade(mix(accent, 0xffffff, 40), mix(accent, 0x000000, 80), dx, half);
        hit = true;
    }
    if (inside(dx, dy, 16, 8)) {  // lid: rim, raised top and sip hole
        *c = color(dx < 3 ? 0xf7f5f1 : 0xdcd7cf);
        if (inside(dx, dy + 1, 13, 6))
            *c = color(inside(dx + 3, dy + 2, 7, 3) ? 0xffffff : 0xeeebe5);
        if (inside(dx + 6, dy - 2, 3, 2))
            *c = color(0x3a2a20);
        hit = true;
    }
    return hit;
}
static bool latte(int dx, int dy, uint16_t* c, unsigned accent) {
    bool hit = cup_shadow(dx, dy, c, 34);
    if (inside(dx, dy - 31, 23, 9)) {  // saucer
        *c = inside(dx, dy - 29, 21, 7) ? color(0xf6f2ea) : color(0xcfc7b8);
        hit = true;
    }
    int half = cup_half(dy, 26);
    if ((dy >= 0 && dy <= 26 && abs(dx) <= half) || inside(dx, dy - 26, 11, 4)) {
        bool stripe = dy >= 9 && dy <= 12;
        *c = shade(stripe ? mix(accent, 0xffffff, 60) : 0xffffff,
                   stripe ? mix(accent, 0x000000, 70) : 0xbdb5a8, dx, half);
        hit = true;
    }
    int hx = dx - 16, hy = dy - 11;  // handle, facing the viewer's right
    if (dx > 12 && inside(hx, hy, 7, 8) && !inside(hx, hy, 3, 4)) {
        *c = color(hx > 2 ? 0xc4bcae : 0xebe5da);
        hit = true;
    }
    if (inside(dx, dy, 15, 8)) {  // rim, crema, coffee and a latte-art heart
        *c = color(dx < 4 ? 0xfffdf8 : 0xe5dfd3);
        if (inside(dx, dy, 13, 6)) {
            *c = color(inside(dx + 1, dy + 1, 10, 4) ? 0x6b3f22 : 0xa8703f);
            int hy2 = dy + 1;
            bool heart = inside(dx + 3, hy2, 4, 3) || inside(dx - 3, hy2, 4, 3) ||
                         (hy2 >= 0 && hy2 <= 4 && abs(dx) <= 6 - hy2 * 3 / 2);
            if (heart)
                *c = color(abs(dx) <= 1 && hy2 <= 0 ? 0xe2c9a6 : 0xf4e6cf);
        }
        hit = true;
    }
    return hit;
}
static bool iced(int dx, int dy, uint16_t* c, unsigned accent) {
    bool hit = cup_shadow(dx, dy, c, 39);
    int half = cup_half(dy, 36);
    if ((dy >= 0 && dy <= 36 && abs(dx) <= half) || inside(dx, dy - 36, 11, 4)) {
        uint16_t glass = blend565(*c, color(0xf4f8fb), 110);
        if (dy >= 7) {  // americano with a slight gradient and ice cubes
            *c = shade(0x6a4128, 0x2a170d, dx, half);
            int ix = (dx + 40) % 9, iy = (dy + 3 * ((dx + 40) / 9)) % 10;
            if (dy < 26 && ix >= 1 && ix <= 6 && iy >= 1 && iy <= 5)
                *c = blend565(*c, color(0xe7f2f7), ix == 1 || iy == 1 ? 150 : 95);
        } else
            *c = glass;
        if (abs(dx) >= half - 1)
            *c = blend565(*c, color(0xffffff), 150);  // glass wall highlight
        hit = true;
    }
    if (inside(dx, dy, 15, 8)) {  // clear rim, coffee surface and three ice cubes
        *c = blend565(*c, color(0xffffff), 170);
        if (inside(dx, dy, 13, 6)) {
            *c = color(inside(dx + 3, dy + 2, 8, 3) ? 0x4a2c1a : 0x3a2214);
            static const int ice[3][2] = {{-5, -1}, {3, -2}, {-1, 2}};
            for (int i = 0; i < 3; ++i) {
                int ix = dx - ice[i][0], iy = dy - ice[i][1];
                if (abs(ix) * 2 + abs(iy) * 3 <= 7)
                    *c = color(iy < 0 || ix < 0 ? 0xf2f9fc : 0xbcd6e2);
            }
        }
        hit = true;
    }
    // Straw from the drink, angled up to the right.
    int sx = dx - 6 - (-dy) / 6;
    if (dy >= -22 && dy <= -2 && sx >= 0 && sx <= 2) {
        *c = color(((dy + 24) / 4) % 2 ? mix(accent, 0xffffff, 110) : accent);
        if (sx == 2)
            *c = blend565(*c, color(0x000000), 50);
        hit = true;
    }
    return hit;
}
static bool laptop(int dx, int dy, uint16_t* c, unsigned accent) {
    bool hit = cup_shadow(dx, dy, c, 12);
    // Screen rises from the deck's upper-left edge: y = -x/2 - 9, x in [-18, 0].
    if (dx >= -18 && dx <= 0) {
        int edge = -dx / 2 - 9;
        if (dy <= edge && dy >= edge - 22) {
            bool bezel = dx <= -17 || dx >= -1 || dy <= edge - 21 || dy >= edge - 1;
            int glow = (edge - dy) * 255 / 22;
            *c = bezel ? color(0x2b2f3a)
                       : color(mix(mix(accent, 0x1b2a4a, 150), 0x9fd3ff, clamp(glow, 0, 255)));
            if (!bezel && (dy - (edge - 15)) == (dx + 9) / 3 && dx < -4)
                *c = color(0xffffff);  // a code line on the screen
            hit = true;
        }
    }
    int d = abs(dx) * 9 + abs(dy) * 18;  // diamond deck, half sizes 18 x 9
    int lower = abs(dx) * 9 + abs(dy - 3) * 18;
    if (lower < 162 && dy > 0) {  // aluminium sides, 3 px thick
        *c = color(dx < 0 ? 0xb9bec8 : 0x8f95a1);
        hit = true;
    }
    if (d < 162) {
        *c = shade(0xeef0f4, 0xc9cdd6, dx, 18);
        if (d < 110 && (dx + 40) % 3 && (dy + 20) % 2 && dy < 3)
            *c = color(0x4a4f5c);  // keys
        if (abs(dx) <= 3 && dy >= 4 && dy <= 6)
            *c = color(0xd7dbe3);  // touchpad
        hit = true;
    }
    return hit;
}
static bool speaker(int dx, int dy, uint16_t* c, unsigned accent) {
    bool hit = cup_shadow(dx, dy, c, 27);
    if ((dy >= 0 && dy <= 24 && abs(dx) <= 14) || inside(dx, dy - 24, 15, 8)) {
        *c = shade(mix(accent, 0x1d1f26, 120), 0x15161b, dx, 14);
        if ((dx + dy * 2 + 60) % 4 == 0 && dy > 2 && dy < 22)
            *c = blend565(*c, color(0x000000), 90);  // fabric weave
        if (dy == 18 && abs(dx) <= 1)
            *c = color(0x7cf2c8);  // status light
        hit = true;
    }
    if (inside(dx, dy, 15, 8)) {  // top: rim, grille, cone and cap
        *c = color(0x2c2f38);
        if (inside(dx, dy, 12, 6))
            *c = (dx + dy + 40) % 2 ? color(0x3d414c) : color(0x24262d);
        if (inside(dx, dy, 6, 3))
            *c = color(mix(accent, 0xffffff, 60));
        hit = true;
    }
    // Two eighth notes floating up and left.
    static const int nx[2] = {-20, -10}, ny[2] = {-10, -20};
    for (int i = 0; i < 2; ++i) {
        int hx = dx - nx[i], hy = dy - ny[i];
        if (inside(hx, hy, 3, 2) || (hx == 2 && hy >= -8 && hy <= 0) ||
            (hy == -8 && hx >= 2 && hx <= 4) || (hy == -7 && hx == 4)) {
            *c = color(mix(accent, 0x000000, 60));
            hit = true;
        }
    }
    return hit;
}
static bool record(int dx, int dy, uint16_t* c, unsigned accent) {
    bool hit = cup_shadow(dx, dy, c, 11);
    if ((dy >= 0 && dy <= 7 && abs(dx) <= 18) || inside(dx, dy - 7, 19, 9)) {  // wooden plinth
        *c = shade(0xc08a55, 0x7a5230, dx, 18);
        hit = true;
    }
    if (inside(dx, dy, 19, 9)) {  // plinth top
        *c = color(0xd8a46c);
        hit = true;
    }
    if (inside(dx, dy - 1, 15, 8) || inside(dx, dy, 15, 8)) {
        *c = color(0x111216);  // vinyl edge, then grooves, sheen and label
        if (inside(dx, dy, 15, 8)) {
            int r = dx * dx + dy * dy * 4;
            *c = color(r % 37 < 4 ? 0x2a2b31 : 0x17181d);
            if (dx < -3 && dy < 0 && dx > -12)
                *c = blend565(*c, color(0x8a8fa0), 50);
            if (inside(dx, dy, 6, 3))
                *c = color(accent);
            if (inside(dx, dy, 1, 1) || (dx == 0 && dy == 0))
                *c = color(0xf2f2f2);
        }
        hit = true;
    }
    // Tone arm resting at the right edge.
    if ((dx >= 14 && dx <= 15 && dy >= -6 && dy <= 2) || (dy == -6 && dx >= 8 && dx <= 15)) {
        *c = color(0xd9dde4);
        hit = true;
    }
    return hit;
}
static void cup_row(uint16_t* out, int x0, int x1, int y, int cx, int cy, int skin) {
    int dy = y - cy;
    if (dy < -32 || dy > 48)
        return;
    static const unsigned accents[6] = {0x2f9e8f, 0xff4a1c, 0x6685a8, 0xe5226b, 0x8d81b5, 0xffc21a};
    unsigned accent = accents[skin % 6];
    int style = skin % 6;
    int start = clamp(cx - 33, x0, x1 + 1), end = clamp(cx + 24, x0 - 1, x1);
    for (int x = start; x <= end; ++x) {
        uint16_t c = out[x - x0];
        int dx = x - cx;
        bool hit = style == CUP_TAKEAWAY  ? takeaway(dx, dy, &c, accent)
                   : style == CUP_LATTE   ? latte(dx, dy, &c, accent)
                   : style == CUP_ICED    ? iced(dx, dy, &c, accent)
                   : style == TINY_LAPTOP ? laptop(dx, dy, &c, accent)
                   : style == TINY_SPEAKER ? speaker(dx, dy, &c, accent)
                                          : record(dx, dy, &c, accent);
        if (hit)
            out[x - x0] = c;
    }
}
static void platform_row(uint16_t* out, int x0, int x1, int y, int cx, int cy, int skin,
                         int size) {
    if (size == JUMP_SIZE_CUP) {
        cup_row(out, x0, x1, y, cx, cy, skin);
        return;
    }
    if (size == JUMP_SIZE_SMALL) {
        // Nearest-neighbour 72 % copy, scaled about the top-face centre.
        int sy = 40 + floor_div((y - cy) * 256, SMALL_SCALE);
        if ((unsigned)sy >= 96)
            return;
        int left = cx - 48 * SMALL_SCALE / 256, right = cx + 47 * SMALL_SCALE / 256;
        int start = x0 > left ? x0 : left, end = x1 < right ? x1 : right;
        const unsigned char* row = jump_sprites + (skin % 6) * JUMP_PLATFORM_BYTES + sy * 96 * 2;
        for (int x = start; x <= end; ++x) {
            int sx = 48 + floor_div((x - cx) * 256, SMALL_SCALE);
            if ((unsigned)sx >= 96)
                continue;
            uint16_t pixel = row[sx * 2] | row[sx * 2 + 1] << 8;
            if (pixel != color(0xdfe1eb))
                out[x - x0] = pixel;
        }
        return;
    }
    int sy = y - cy + 40;
    if ((unsigned)sy >= 96)
        return;
    int left = cx - 48, start = x0 > left ? x0 : left;
    int end = x1 < left + 95 ? x1 : left + 95;
    if (start > end)
        return;
    const unsigned char* p =
        jump_sprites + (skin % 6) * JUMP_PLATFORM_BYTES + (sy * 96 + start - left) * 2;
    for (int x = start; x <= end; ++x, p += 2) {
        uint16_t pixel = p[0] | p[1] << 8;
        if (pixel != color(0xdfe1eb))
            out[x - x0] = pixel;
    }
}
static int ease(int t, int duration) {
    int q = clamp(t, 0, duration) * 256 / duration;
    return q * q * (768 - 2 * q) / 65536;
}
static void camera(const nabo_jump_t* g, uint32_t now, int* x, int* y) {
    *x = g->dir * g->gap / 2;
    *y = 0;
    if (g->phase == JUMP_SCROLLING) {
        int e = ease((int)(now - g->changed_at) - scroll_settle(g), 280);
        *x += (g->dir * g->gap + g->future_dir * g->future_gap / 2 - *x) * e / 256;
        *y = -g->gap * e / 512;
    }
}
bool nabo_jump_camera_moving(const nabo_jump_t* g, uint32_t now) {
    return g->phase == JUMP_SCROLLING && now - g->changed_at >= (unsigned)scroll_settle(g);
}
bool nabo_jump_charging(const nabo_jump_t* g) {
    return g->phase == JUMP_CHARGING || g->pending_charge;
}
uint32_t nabo_jump_charge_ms(const nabo_jump_t* g, uint32_t now) {
    if (g->pending_charge)
        return g->pending_released ? (uint32_t)g->pending_hold : now - g->pending_at;
    return g->phase == JUMP_CHARGING ? now - g->charge_at : 0;
}
void nabo_jump_character_bounds(const nabo_jump_t* g, uint32_t now, int* x0, int* y0, int* x1,
                                int* y1) {
    int cx, cy;
    camera(g, now, &cx, &cy);
    int x = 120 + g->player_x - cx, y = 216 + g->player_y - cy;
    int half_width = g->phase == JUMP_FALLING || g->phase == JUMP_OVER ? 46 : 30;
    *x0 = clamp(x - half_width, 0, 239);
    *x1 = clamp(x + half_width, 0, 239);
    int top = y - 3 - g->lift - 80;
    // Keep the old shadow in the dirty bounds until it has faded out.
    bool shadow_visible = true;
    if (shadow_visible && top > y - 7)
        top = y - 7;
    *y0 = clamp(top, 0, 319);
    *y1 = clamp(y + 8 + (g->lift < 0 ? -g->lift : 0), 0, 319);
    if (g->phase == JUMP_FALLING || g->phase == JUMP_OVER)
        *y1 = clamp(y + g->fall_ground_drop + 8, 0, 319);
}
void nabo_jump_scene_bounds(const nabo_jump_t* g, uint32_t now, int* x0, int* y0, int* x1,
                            int* y1) {
    nabo_jump_character_bounds(g, now, x0, y0, x1, y1);
    int cx, cy;
    camera(g, now, &cx, &cy);
    int xs[3] = {120 - cx, 120 + g->dir * g->gap - cx,
                 120 + g->dir * g->gap + g->future_dir * g->future_gap - cx};
    int ys[3] = {216 - cy, 216 - g->gap / 2 - cy, 216 - g->gap / 2 - g->future_gap / 2 - cy};
    int count = nabo_jump_camera_moving(g, now) ? 3 : 2;
    for (int i = 0; i < count; ++i) {
        if (xs[i] - 48 < *x0)
            *x0 = xs[i] - 48;
        if (xs[i] + 47 > *x1)
            *x1 = xs[i] + 47;
        if (ys[i] - 40 < *y0)
            *y0 = ys[i] - 40;
        if (ys[i] + 55 > *y1)
            *y1 = ys[i] + 55;
    }
    *x0 = clamp(*x0, 0, 239);
    *x1 = clamp(*x1, 0, 239);
    *y0 = clamp(*y0, 0, 319);
    *y1 = clamp(*y1, 0, 319);
}
void nabo_jump_bounds(const nabo_jump_t* g, uint32_t now, int* x0, int* y0, int* x1, int* y1) {
    nabo_jump_character_bounds(g, now, x0, y0, x1, y1);
    if (g->phase == JUMP_SCROLLING) {
        *x0 = 0;
        *y0 = 75;
        *x1 = 239;
        *y1 = 319;
    }
    if (g->phase == JUMP_CHARGING) {
        *x0 = 30;
        *x1 = 210;
        *y1 = 286;
    }
}
void nabo_jump_draw_row(const nabo_jump_t* g, uint32_t now, int y, int x0, int x1, uint16_t* out) {
    int camera_x, camera_y;
    camera(g, now, &camera_x, &camera_y);
    int base_x = 120, base_y = 216;
    int next_x = base_x + g->dir * g->gap, next_y = base_y - g->gap / 2;
    int px = base_x + g->player_x - camera_x, py = base_y + g->player_y - camera_y;
    int foot = py - 3 - g->lift;
    int body_h = 21, leg_h = 15, bend = 0, lean = 0;
    int rotation_sin = 0, rotation_cos = 1024, fall_alpha = 255, shadow_alpha = 100;
    bool falling = g->phase == JUMP_FALLING || g->phase == JUMP_OVER;
    if (g->phase == JUMP_CHARGING) {
        int q = ease((int)(now - g->charge_at), 650);
        body_h -= 4 * q / 256;
        leg_h -= 7 * q / 256;
        bend = 3 * q / 256;
        lean = g->dir * q / 256;
    } else if (g->phase == JUMP_FLYING) {
        unsigned t = now - g->changed_at;
        if (t < 85) {
            int q = (85 - (int)t) * 256 / 85;
            body_h += 3 * q / 256;
            leg_h += 4 * q / 256;
        } else if (t < 315) {
            leg_h = 11;
            bend = 3;
            lean = g->dir * 2;
        } else {
            int q = ((int)t - 315) * 256 / 85;
            leg_h = 11 + 4 * clamp(q, 0, 256) / 256;
            lean = g->dir;
        }
    } else if (g->phase == JUMP_SCROLLING) {
        int t = (int)(now - g->changed_at);
        if (t < 90) {
            int q = 256 - ease(t, 90);
            body_h -= 3 * q / 256;
            leg_h -= 7 * q / 256;
            bend = 3 * q / 256;
        } else if (t < 200) {
            int q = 256 - ease(t - 90, 110);
            body_h += q / 128;
            leg_h += q / 128;
        }
    } else if (falling) {
        static const int sin_q10[] = {0,   89,  178, 265, 350, 433, 512,
                                      587, 658, 724, 784, 839, 887, 928};
        static const int cos_q10[] = {1024, 1020, 1008, 989, 962, 928, 887,
                                      839,  784,  724,  658, 587, 512, 433};
        int t = clamp((int)(now - g->changed_at), 0, 560);
        int side = g->dir * (g->distance >= g->gap ? 1 : -1);
        int angle = 12 * ease(t, 260), index = angle / 256, fraction = angle % 256;
        rotation_sin =
            side * (sin_q10[index] + (sin_q10[index + 1] - sin_q10[index]) * fraction / 256);
        rotation_cos = cos_q10[index] + (cos_q10[index + 1] - cos_q10[index]) * fraction / 256;
        fall_alpha = 255;
        shadow_alpha = 100;
        body_h += 2 * ease(t, 220) / 256;
        leg_h -= 2 * ease(t, 160) / 256;
        bend = 3 * ease(t, 140) / 256;
        lean = g->dir * (256 - ease(t, 200)) / 256;
    }
    int head_h = 28, total_h = head_h + body_h + leg_h;
    for (int x = x0; x <= x1; ++x)
        out[x - x0] = color(0xdfe1eb);
    if (nabo_jump_camera_moving(g, now))
        platform_row(out, x0, x1, y, next_x + g->future_dir * g->future_gap - camera_x,
                     next_y - g->future_gap / 2 - camera_y, g->future_skin, g->future_size);
    platform_row(out, x0, x1, y, next_x - camera_x, next_y - camera_y, g->next_skin,
                 g->next_size);
    platform_row(out, x0, x1, y, base_x - camera_x, base_y - camera_y, g->skin, g->size);
    if (g->character == 0) {
        int shadow_y = falling ? py - 3 + g->fall_ground_drop + 2 : py - 2;
        for (int x = clamp(px - 12, x0, x1 + 1); x <= clamp(px + 12, x0 - 1, x1); ++x)
            if (ellipse(x - px, y - shadow_y, 12, 5))
                out[x - x0] = blend565(out[x - x0], color(0x5e637a), 100);
        nabo_jump_rig_draw_row(g, now, px, foot, y, x0, x1, out);
        if (nabo_jump_charging(g) && y >= 281 && y <= 286) {
            int fill = clamp((int)nabo_jump_charge_ms(g, now), 0, 1400) * 180 / 1400;
            for (int x = clamp(30, x0, x1 + 1); x <= clamp(210, x0 - 1, x1); ++x)
                out[x - x0] = color(x < 30 + fill ? 0x6685a8 : 0xc3c7d7);
        }
        return;
    }
    int half_width = falling ? 46 : 23;
    int start = x0 > px - half_width ? x0 : px - half_width;
    int end = x1 < px + half_width ? x1 : px + half_width;
    int pivot_y = foot - (g->character == 0 ? total_h / 2 : 24);
    bool body_row = falling ? abs(y - pivot_y) <= 43 : y >= foot - total_h && y <= foot + 4;
    if (!(body_row || abs(y - py + 2) <= 5))
        end = start - 1;
    for (int x = start; x <= end; ++x) {
        uint16_t c = out[x - x0];
        int dx = x - px, draw_y = y;
        if (rotation_sin) {
            int dy = y - pivot_y;
            // Inverse rigid-body rotation around the character's center.
            // Lookup + integer interpolation avoids floating-point trig on C3.
            draw_y = pivot_y + (-dx * rotation_sin + dy * rotation_cos) / 1024;
            dx = (dx * rotation_cos + dy * rotation_sin) / 1024;
        }
        if (abs(x - px) <= 12 && abs(y - py + 2) <= 5 && ellipse(x - px, y - py + 2, 12, 5))
            c = blend565(c, color(0x5e637a), shadow_alpha);
        uint16_t background = c;
        if (g->character == 0 && draw_y >= foot - total_h && draw_y < foot && abs(dx) <= 23) {
            int local_y = draw_y - (foot - total_h), sy, sx = dx + JUMP_CHARACTER_WIDTH / 2;
            if (local_y < head_h) {
                sy = local_y;
                sx -= lean;
            } else if (local_y < head_h + body_h) {
                sy = 28 + (local_y - head_h) * 21 / body_h;
                sx -= lean * (head_h + body_h - local_y) / body_h;
            } else {
                int ly = local_y - head_h - body_h;
                sy = 49 + ly * 15 / leg_h;
                int knee = 4 * ly * (leg_h - ly) * bend / (leg_h * leg_h);
                sx -= dx < 0 ? -knee : knee;
            }
            if (sx >= 0 && sx < JUMP_CHARACTER_WIDTH && sy >= 0 && sy < JUMP_CHARACTER_HEIGHT)
                c = sprite(
                    c, jump_sprites + JUMP_CHARACTER_OFFSET + (sy * JUMP_CHARACTER_WIDTH + sx) * 3);
        } else if (g->character == 1 && abs(dx) <= 10 && draw_y >= foot - 48 &&
                   draw_y <= foot + 4) {
            int h = 24 - (15 - leg_h);
            if (draw_y >= foot - h && draw_y <= foot &&
                abs(dx) <= 5 + (draw_y - (foot - h)) * 4 / h)
                c = color(dx < 0 ? 0x62518b : 0x403658);
            if (ellipse(dx, draw_y - foot + 1, 9, 4))
                c = color(0x403658);
            if (ellipse(dx, draw_y - (foot - h - 5), 9, 9)) {
                int dy = draw_y - (foot - h - 5);
                c = color(mix(0x332d4d, 0x8d81b5, clamp(170 - dx * 7 - dy * 7, 25, 220)));
                if (ellipse(dx + 3, dy + 4, 3, 2))
                    c = color(0xc3b7e7);
            }
        }
        if (fall_alpha < 255)
            c = blend565(background, c, fall_alpha);
        out[x - x0] = c;
    }
    if (nabo_jump_charging(g) && y >= 281 && y <= 286) {
        int fill = clamp((int)nabo_jump_charge_ms(g, now), 0, 1400) * 180 / 1400;
        int left = x0 > 30 ? x0 : 30, right = x1 < 210 ? x1 : 210;
        for (int x = left; x <= right; ++x)
            out[x - x0] = color(x < 30 + fill ? 0x6685a8 : 0xc3c7d7);
    }
}
