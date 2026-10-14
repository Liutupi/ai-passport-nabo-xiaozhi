#pragma once
/* Procedural dice: roll plan, fixed-point 3D strip renderer and synthesized
 * clatter. Pure C with no ESP-IDF, LVGL, heap or float use, so the same code
 * runs on the ESP32-C3 (no FPU) and in host tests/previews. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NABO_DICE_MAX 3
#define NABO_DICE_W 224          /* sprite: the whole felt tray */
#define NABO_DICE_H 196
#define NABO_DICE_IMPACTS_MAX 24

typedef struct {
    uint32_t seed;
    uint8_t count;
    uint8_t value[NABO_DICE_MAX];
    int16_t half;                         /* half edge, px */
    int16_t tx[NABO_DICE_MAX], ty[NABO_DICE_MAX];   /* resting table anchor */
    int16_t sx[NABO_DICE_MAX], sy[NABO_DICE_MAX];   /* anchor when thrown */
    uint16_t yaw[NABO_DICE_MAX];          /* final yaw, 65536 = one turn */
    int16_t axis[NABO_DICE_MAX][3];       /* Q14 unit spin axis (world) */
    int32_t spin[NABO_DICE_MAX];          /* total spin, 65536 = one turn */
    uint16_t peak[NABO_DICE_MAX];         /* first arc height, px */
    uint16_t arc_ms[NABO_DICE_MAX];       /* first full arc duration */
    uint16_t land_ms[NABO_DICE_MAX];      /* settled from here on */
    uint16_t end_ms;                      /* every die settled */
} nabo_dice_plan_t;

typedef struct {
    int32_t fcx, fcy;          /* face centre, Q8 px */
    int32_t ux, uy, vx, vy;    /* d(u,v)/d(px), Q16 */
    int32_t gu, gv;            /* px per unit u / v, Q8 (anti-aliasing) */
    int32_t mu, mv;            /* one pixel in u / v, Q16 */
    int16_t col[3];            /* flat face colour, Q4 */
    int16_t dcol[4][3];        /* rounded-edge colour delta: u+, u-, v+, v- */
    int16_t lit;               /* pip light, Q8 */
    int16_t lu, lv;            /* light across the face, Q14 */
    uint8_t sil;               /* silhouette edges: bit0 u+, 1 u-, 2 v+, 3 v- */
    uint8_t value;
    uint8_t flat;              /* bevel >= 0.5 px: interior is opaque flat colour */
} nabo_dice_face_t;

typedef struct {
    int16_t bx0, by0, bx1, by1;          /* die bounds, px inclusive */
    int16_t sx0, sy0, sx1, sy1;          /* shadow bounds */
    int32_t shx, shy;                    /* shadow centre, Q8 */
    int32_t shrx, shry;                  /* 1/radius, Q24 per Q8 px */
    int16_t sha;                         /* shadow strength, Q8 */
    uint8_t faces;
    nabo_dice_face_t face[3];
} nabo_dice_die_t;

typedef struct {
    uint8_t count;
    uint8_t order[NABO_DICE_MAX];        /* back to front */
    nabo_dice_die_t die[NABO_DICE_MAX];
} nabo_dice_scene_t;

/* Deterministic: the UI and the voice task rebuild the same roll from seed. */
void nabo_dice_plan(nabo_dice_plan_t *plan, uint32_t seed, unsigned count);
unsigned nabo_dice_total(const nabo_dice_plan_t *plan);

/* Pose at t ms after the throw; t >= end_ms is the resting result. */
void nabo_dice_scene(nabo_dice_scene_t *scene, const nabo_dice_plan_t *plan, int32_t t_ms);
/* Union of every die and shadow, clipped to the sprite. False when empty. */
bool nabo_dice_bounds(const nabo_dice_scene_t *scene, int *x0, int *y0, int *x1, int *y1);
/* One die with its shadow; false when i is not a die of this scene. */
bool nabo_dice_die_bounds(const nabo_dice_scene_t *scene, unsigned i, int *x0, int *y0, int *x1, int *y1);
/* One sprite row, inclusive x range, native RGB565. */
void nabo_dice_draw_row(const nabo_dice_scene_t *scene, int y, int x0, int x1, uint16_t *dst);

/* Table hits for the sound, sorted by time. Amplitude is Q8 (256 = hardest). */
typedef struct { uint16_t ms; uint16_t amp; } nabo_dice_impact_t;
unsigned nabo_dice_impacts(const nabo_dice_plan_t *plan, nabo_dice_impact_t *out, unsigned cap);

/* 16 kHz mono clatter synthesized on the fly, no Flash samples. */
typedef struct {
    nabo_dice_impact_t hit[NABO_DICE_IMPACTS_MAX];
    uint8_t hits, next;
    uint32_t at, end, noise;
    int32_t burst, burst_amp;
    int32_t y[4][2];
} nabo_dice_sound_t;

void nabo_dice_sound_init(nabo_dice_sound_t *sound, const nabo_dice_plan_t *plan, uint32_t start_frame);
/* Returns frames written (<= n); 0 once the clatter has died away. */
size_t nabo_dice_sound_fill(nabo_dice_sound_t *sound, int16_t *pcm, size_t n);
