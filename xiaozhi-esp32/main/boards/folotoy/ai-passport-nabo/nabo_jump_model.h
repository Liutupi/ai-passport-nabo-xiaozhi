#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    JUMP_READY,
    JUMP_CHARGING,
    JUMP_FLYING,
    JUMP_SCROLLING,
    JUMP_FALLING,
    JUMP_OVER
} jump_phase_t;
typedef struct {
    jump_phase_t phase;
    uint32_t rng, score, best, combo, changed_at, charge_at;
    int dir, gap, skin, next_skin, distance, player_x, player_y, lift, character, future_dir,
        future_gap, future_skin;
    int fall_contact_ms, fall_ground_drop, fall_target_x;
    uint32_t pending_at;
    int pending_hold;
    bool landed, centered, sound, pending_charge, pending_released;
} nabo_jump_t;
void nabo_jump_reset(nabo_jump_t* game, uint32_t seed, uint32_t best);
void nabo_jump_press(nabo_jump_t* game, uint32_t now);
void nabo_jump_release(nabo_jump_t* game, uint32_t now);
bool nabo_jump_tick(nabo_jump_t* game, uint32_t now);
void nabo_jump_draw_row(const nabo_jump_t* game, uint32_t now, int y, int x0, int x1,
                        uint16_t* out);

void nabo_jump_bounds(const nabo_jump_t* game, uint32_t now, int* x0, int* y0, int* x1, int* y1);
void nabo_jump_character_bounds(const nabo_jump_t* game, uint32_t now, int* x0, int* y0, int* x1,
                                int* y1);
void nabo_jump_scene_bounds(const nabo_jump_t* game, uint32_t now, int* x0, int* y0, int* x1,
                            int* y1);
bool nabo_jump_camera_moving(const nabo_jump_t* game, uint32_t now);
bool nabo_jump_charging(const nabo_jump_t* game);
uint32_t nabo_jump_charge_ms(const nabo_jump_t* game, uint32_t now);
