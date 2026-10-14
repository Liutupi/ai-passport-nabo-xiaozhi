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
            // The diagonal landing axis crosses the rendered top within 16px.
            if (abs(delta) <= 16) {
                g->landed = true;
                g->centered = abs(delta) <= 5;
                if (g->centered) {
                    if (g->combo < 1000)
                        ++g->combo;
                } else
                    g->combo = 0;
                g->score += g->centered ? 2 * g->combo : 1;
                if (g->score > g->best)
                    g->best = g->score;
                g->future_skin = (int)(random_next(g) % 6);
                g->future_dir = (random_next(g) & 1) ? 1 : -1;
                g->future_gap = 65 + (int)(random_next(g) % 30);
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
static void platform_row(uint16_t* out, int x0, int x1, int y, int cx, int cy, int skin) {
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
                     next_y - g->future_gap / 2 - camera_y, g->future_skin);
    platform_row(out, x0, x1, y, next_x - camera_x, next_y - camera_y, g->next_skin);
    platform_row(out, x0, x1, y, base_x - camera_x, base_y - camera_y, g->skin);
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
