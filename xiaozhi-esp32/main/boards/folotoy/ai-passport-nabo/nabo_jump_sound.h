#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t position, step;
    unsigned offset, length;
    int kind, last_sample, fade_from;
    unsigned fade_remaining;
} nabo_jump_sound_t;
void nabo_jump_sound_start(nabo_jump_sound_t* sound, int kind, unsigned combo);
void nabo_jump_sound_fill(nabo_jump_sound_t* sound, int16_t* pcm, size_t frames);
