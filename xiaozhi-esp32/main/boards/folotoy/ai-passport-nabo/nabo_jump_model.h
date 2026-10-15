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
    /* Platform size (JUMP_SIZE_*) of the current, next and queued platforms;
     * steps counts landings and drives the difficulty ramp. */
    int size, next_size, future_size, steps;
} nabo_jump_t;
/* JUMP_SIZE_CUP is the one-person item; its skin picks takeaway coffee, latte,
 * iced americano, laptop, speaker or vinyl record. */
enum { JUMP_SIZE_NORMAL, JUMP_SIZE_SMALL, JUMP_SIZE_CUP, JUMP_SIZE_COUNT };
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
/* Landing half-width along the jump axis, and the centered (combo) window. */
int nabo_jump_tolerance(int size);
int nabo_jump_center_tolerance(int size);
/* Difficulty for the platform generated after `steps` landings (pure). */
void nabo_jump_difficulty(int steps, int* gap_min, int* gap_max, int* small_pct, int* cup_pct);
