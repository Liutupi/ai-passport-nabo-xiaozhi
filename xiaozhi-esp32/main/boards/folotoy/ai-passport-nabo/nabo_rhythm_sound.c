#include "nabo_rhythm_sound.h"
#include <string.h>

/* Quarter-wave sine, 65 points, amplitude 32767. */
static const int16_t quarter_sine[65] = {
    0,     804,   1608,  2410,  3212,  4011,  4808,  5602,  6393,  7179,  7962,  8739,  9512,
    10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868,
    19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811, 25329, 25832, 26319,
    26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956, 30273, 30571, 30852, 31113,
    31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757, 32767};

static int sine(uint32_t phase) {
    unsigned index = phase >> 24; /* 0..255 */
    unsigned quadrant = index >> 6, offset = index & 63;
    int value = quadrant & 1 ? quarter_sine[64 - offset] : quarter_sine[offset];
    return quadrant & 2 ? -value : value;
}

typedef struct {
    uint16_t hz;
    uint16_t level;   /* peak, 0..32767 */
    uint16_t tau_ms;  /* exponential decay time constant */
    uint16_t length_ms;
    uint8_t harmonic;
} tone_t;

/* Per-sample decay multipliers (Q16) for exp(-1 / (tau * 16 kHz)). */
static int32_t decay_for(unsigned tau_ms) {
    /* 1 - 1/x is accurate to < 0.2 % over the ~100 ms notes used here. */
    uint32_t samples = tau_ms * 16u;
    return (int32_t)(65536u - 65536u / (samples ? samples : 1));
}

static void start(nabo_rhythm_sound_t* synth, const tone_t* tone, uint32_t delay) {
    rh_voice_t* slot = &synth->voices[0];
    for (int i = 0; i < RH_VOICES; ++i) {
        rh_voice_t* v = &synth->voices[i];
        if (!v->remaining) {
            slot = v;
            break;
        }
        if (v->amp < slot->amp)
            slot = v; /* steal the quietest voice */
    }
    slot->phase = 0;
    slot->step = (uint32_t)((uint64_t)tone->hz * 4294967296ull / 16000u);
    slot->delay = delay;
    slot->remaining = tone->length_ms * 16u;
    slot->amp = (int32_t)tone->level << 1; /* Q16 of level/32768 */
    slot->decay = decay_for(tone->tau_ms);
    slot->age = 0;
    slot->harmonic = tone->harmonic;
}

void nabo_rhythm_sound_reset(nabo_rhythm_sound_t* synth) { memset(synth, 0, sizeof(*synth)); }

void nabo_rhythm_sound_play(nabo_rhythm_sound_t* synth, rh_sound_t sound, uint32_t delay) {
    static const tone_t tones[] = {
        [RH_SND_TICK] = {1760, 6500, 9, 45, 0},
        [RH_SND_ACCENT] = {2349, 9000, 12, 60, 0},
        [RH_SND_SOFT_TICK] = {1760, 3000, 8, 40, 0},
        [RH_SND_LOW] = {523, 15000, 110, 420, 3},
        [RH_SND_HIGH] = {784, 14000, 100, 380, 3},
        [RH_SND_TAP_LOW] = {523, 12500, 90, 340, 3},
        [RH_SND_TAP_HIGH] = {784, 11500, 85, 320, 3},
        [RH_SND_PERFECT] = {2093, 5200, 45, 160, 0},
        [RH_SND_MISS] = {147, 11000, 70, 220, 4},
        [RH_SND_MOVE] = {1319, 4500, 10, 50, 0},
    };
    static const tone_t turn[] = {{1047, 6000, 60, 200, 1}, {1568, 5000, 70, 240, 1}};
    static const tone_t clear[] = {
        {523, 12000, 110, 360, 2}, {659, 12000, 110, 360, 2}, {784, 12000, 120, 400, 2}, {1047, 12500, 180, 700, 2}};
    static const tone_t fail[] = {{392, 11000, 120, 360, 3}, {330, 11000, 120, 360, 3}, {262, 12000, 200, 650, 3}};
    switch (sound) {
        case RH_SND_TURN:
            start(synth, &turn[0], delay);
            start(synth, &turn[1], delay + 70 * 16);
            break;
        case RH_SND_CLEAR:
            for (unsigned i = 0; i < 4; ++i)
                start(synth, &clear[i], delay + i * 95 * 16);
            break;
        case RH_SND_FAIL:
            for (unsigned i = 0; i < 3; ++i)
                start(synth, &fail[i], delay + i * 140 * 16);
            break;
        case RH_SND_MISS: {
            tone_t detuned = tones[RH_SND_MISS];
            start(synth, &detuned, delay);
            detuned.hz = 156;
            start(synth, &detuned, delay);
            break;
        }
        default:
            if (sound >= 0 && sound < RH_SND_COUNT && tones[sound].hz)
                start(synth, &tones[sound], delay);
            break;
    }
}

void nabo_rhythm_sound_fill(nabo_rhythm_sound_t* synth, int16_t* pcm, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
        int32_t mix = 0;
        for (int n = 0; n < RH_VOICES; ++n) {
            rh_voice_t* v = &synth->voices[n];
            if (!v->remaining)
                continue;
            if (v->delay) {
                --v->delay;
                continue;
            }
            int wave = sine(v->phase);
            if (v->harmonic)
                wave = (wave * (8 - v->harmonic) + sine(v->phase * 2) * v->harmonic) / 8;
            /* 4 ms attack avoids clicks at note starts. */
            int32_t gain = v->amp;
            if (v->age < 64)
                gain = gain * (int32_t)v->age++ / 64;
            mix += (int32_t)(((int64_t)wave * gain) >> 16);
            v->phase += v->step;
            v->amp = (int32_t)(((int64_t)v->amp * v->decay) >> 16);
            if (!--v->remaining || v->amp < 8)
                v->remaining = 0;
        }
        pcm[i] = (int16_t)(mix > 32767 ? 32767 : mix < -32768 ? -32768 : mix);
    }
}
