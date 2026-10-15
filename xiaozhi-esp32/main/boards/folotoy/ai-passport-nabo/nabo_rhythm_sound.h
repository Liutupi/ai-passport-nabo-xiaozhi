#pragma once
/* Tiny 16 kHz mono synthesizer for the rhythm game: decaying sine plucks,
 * metronome clicks and short jingles. Fixed voice pool, no heap. */
#include <stddef.h>
#include <stdint.h>

#define RH_VOICES 8

typedef enum {
    RH_SND_TICK,
    RH_SND_ACCENT,
    RH_SND_SOFT_TICK,
    RH_SND_LOW,
    RH_SND_HIGH,
    RH_SND_TAP_LOW,
    RH_SND_TAP_HIGH,
    RH_SND_PERFECT,
    RH_SND_MISS,
    RH_SND_TURN,
    RH_SND_CLEAR,
    RH_SND_FAIL,
    RH_SND_MOVE,
    RH_SND_COUNT
} rh_sound_t;

typedef struct {
    uint32_t phase, step, delay, remaining;
    int32_t amp, decay; /* Q16 amplitude and per-sample multiplier */
    uint16_t age;       /* samples played, for the attack ramp */
    uint8_t harmonic;   /* second harmonic level in 1/8ths */
} rh_voice_t;

typedef struct {
    rh_voice_t voices[RH_VOICES];
} nabo_rhythm_sound_t;

void nabo_rhythm_sound_reset(nabo_rhythm_sound_t* synth);
/* Starts a sound delay_samples into the next fill() call. */
void nabo_rhythm_sound_play(nabo_rhythm_sound_t* synth, rh_sound_t sound, uint32_t delay_samples);
void nabo_rhythm_sound_fill(nabo_rhythm_sound_t* synth, int16_t* pcm, size_t frames);
