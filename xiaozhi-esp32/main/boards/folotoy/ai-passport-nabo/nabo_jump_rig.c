/* Articulated NABO: original face/cap/coat, independent shoulders/elbows/hips/
 * knees/ankles. Integer rendering only; never scale the complete character. */
#include "nabo_jump_rig.h"
#include <stdlib.h>
#include "nabo_jump_assets.h"
static int clamp(int x, int a, int b) { return x < a ? a : x > b ? b : x; }
static int ease(int t, int d) {
    int q = clamp(t, 0, d) * 256 / d;
    return q * q * (768 - 2 * q) / 65536;
}
static int lerp(int a, int b, int q) { return a + (b - a) * q / 256; }
static uint16_t color(unsigned c) {
    return (uint16_t)(((c >> 19) & 31) << 11 | ((c >> 10) & 63) << 5 | ((c >> 3) & 31));
}
static uint16_t blend(uint16_t bg, uint16_t fg, unsigned a) {
    if (!a)
        return bg;
    if (a == 255)
        return fg;
    unsigned r = (((bg >> 11) & 31) * (255 - a) + ((fg >> 11) & 31) * a + 127) / 255;
    unsigned g = (((bg >> 5) & 63) * (255 - a) + ((fg >> 5) & 63) * a + 127) / 255;
    unsigned b = ((bg & 31) * (255 - a) + (fg & 31) * a + 127) / 255;
    return (uint16_t)(r << 11 | g << 5 | b);
}
static void bone(uint16_t* out, int x0, int x1, int y, int ax, int ay, int bx, int by, int r,
                 unsigned fill, unsigned edge) {
    if (y < (ay < by ? ay : by) - r || y > (ay > by ? ay : by) + r)
        return;
    int left = clamp((ax < bx ? ax : bx) - r, x0, x1 + 1),
        right = clamp((ax > bx ? ax : bx) + r, x0 - 1, x1);
    int vx = bx - ax, vy = by - ay, len = vx * vx + vy * vy;
    for (int x = left; x <= right; ++x) {
        int dx = x - ax, dy = y - ay, dot = dx * vx + dy * vy, numerator, denominator = 1;
        if (dot <= 0 || !len)
            numerator = dx * dx + dy * dy;
        else if (dot >= len) {
            dx = x - bx;
            dy = y - by;
            numerator = dx * dx + dy * dy;
        } else {
            int cross = dx * vy - dy * vx;
            numerator = cross * cross;
            denominator = len;
        }
        if (numerator <= r * r * denominator)
            out[x - x0] = color(numerator > (r - 1) * (r - 1) * denominator ? edge : fill);
    }
}
static void oval(uint16_t* out, int x0, int x1, int y, int cx, int cy, int rx, int ry,
                 unsigned fill, unsigned edge) {
    if (abs(y - cy) > ry)
        return;
    for (int x = clamp(cx - rx, x0, x1 + 1); x <= clamp(cx + rx, x0 - 1, x1); ++x) {
        int dx = x - cx, dy = y - cy, d = dx * dx * ry * ry + dy * dy * rx * rx;
        if (d <= rx * rx * ry * ry)
            out[x - x0] = color(d > (rx - 1) * (rx - 1) * ry * ry ? edge : fill);
    }
}
void nabo_jump_rig_draw_row(const nabo_jump_t* g, uint32_t now, int px, int foot, int y, int x0,
                            int x1, uint16_t* out) {
    int t = (int)(now - g->changed_at), squat = 0, fold = 0, arm = 0, lean = 0, sit = 0;
    if (g->phase == JUMP_CHARGING) {
        squat = ease((int)(now - g->charge_at), 500);
        lean = 6 * squat / 256;
    } else if (g->phase == JUMP_FLYING) {
        if (t < 80) {
            int q = ease(t, 80);
            int charged = ease((int)(g->changed_at - g->charge_at), 500);
            squat = charged * (256 - q) / 256;
            arm = q;
            lean = lerp(6 * charged / 256, 8, q);
        } else {
            fold = (256 - ease(t - 300, 100)) * ease(t - 80, 100) / 256;
            arm = 256 - ease(t - 290, 110);
            lean = 3 * (256 - ease(t - 300, 100)) / 256;
        }
    } else if (g->phase == JUMP_SCROLLING) {
        squat = t < 70 ? ease(t, 70) : 256 - ease(t - 70, 110);
        if (g->pending_charge) {
            int charged = ease((int)nabo_jump_charge_ms(g, now), 500);
            if (charged > squat)
                squat = charged;
        }
        lean = 4 * squat / 256;
    } else if (g->phase == JUMP_FALLING || g->phase == JUMP_OVER) {
        sit = g->phase == JUMP_OVER ? 256 : ease(t - g->fall_contact_ms, 100);
        arm = 128 * (256 - sit) / 256;
        lean = lerp(5, 9, sit);
    }
    int hip_y = foot - 15 + 7 * squat / 256 + 9 * sit / 256,
        hip_x = px + g->dir * (2 * squat / 256 + 3 * sit / 256);
    int body_sin = g->dir * lean * 18, body_cos = 1024 - body_sin * body_sin / 2048;
    for (int side = -1; side <= 1; side += 2) {
        int hx = hip_x + side * 4,
            kx = px + side * 5 + g->dir * (6 * squat / 256 + 8 * fold / 256 + 8 * sit / 256);
        int ky = foot - 8 + 4 * squat / 256 - 8 * fold / 256 + 2 * sit / 256;
        int ax = px + side * 6 + g->dir * (3 * fold / 256 + 9 * sit / 256),
            ay = foot - 2 - 8 * fold / 256;
        bone(out, x0, x1, y, hx, hip_y, kx, ky, 3, 0x394966, 0x293b58);
        bone(out, x0, x1, y, kx, ky, ax, ay, 3, 0x394966, 0x293b58);
        oval(out, x0, x1, y, ax + g->dir, ay + 2, 4, 2, 0xf7f6f4, 0x9aa3b1);
        int sx = hip_x + side * 8, sy = hip_y - 19;
        int ex = sx + g->dir * lerp(-3 * squat / 256, 5, arm) + side * 3 * sit / 256;
        int ey = sy + lerp(9 - 2 * squat / 256, -3, arm) + 4 * sit / 256;
        int wx = sx + g->dir * lerp(-6 * squat / 256, 10, arm) + side * 5 * sit / 256;
        int wy = lerp(sy + lerp(17 - 4 * squat / 256, -9, arm), foot - 4, sit);
        bone(out, x0, x1, y, sx, sy, ex, ey, 3, 0xf3f4f9, 0xc9cfdb);
        bone(out, x0, x1, y, ex, ey, wx, wy, 2, 0xf3f4f9, 0xc9cfdb);
        oval(out, x0, x1, y, wx, wy + 1, 2, 2, 0xf2bfaa, 0xd89b8d);
    }
    if (y < hip_y - 53 || y > hip_y + 6)
        return;
    for (int x = clamp(hip_x - 38, x0, x1 + 1); x <= clamp(hip_x + 38, x0 - 1, x1); ++x) {
        int dx = x - hip_x, dy = y - hip_y, sx = 18 + (dx * body_cos + dy * body_sin) / 1024,
            sy = 49 + (-dx * body_sin + dy * body_cos) / 1024;
        if (sx < 0 || sx >= 36 || sy < 0 || sy > 50 || (sy >= 28 && (sx < 10 || sx > 26)))
            continue;
        const unsigned char* p = jump_sprites + JUMP_CHARACTER_OFFSET + (sy * 36 + sx) * 3;
        out[x - x0] = blend(out[x - x0], (uint16_t)(p[0] | p[1] << 8), p[2]);
    }
}
