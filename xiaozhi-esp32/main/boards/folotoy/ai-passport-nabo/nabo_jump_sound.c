/* Community jump/center PCM and a quieter, faded variant of its charge sweep.
 * See game-art/original-sound-reference.json for provenance and checksums.
 * Stream directly from flash; no complete audio buffer or additional task. */
#include "nabo_jump_sound.h"
#include <string.h>
extern const unsigned char jump_sounds[] __asm__("_binary_jump_sounds_bin_start");

void nabo_jump_sound_start(nabo_jump_sound_t* s, int kind, unsigned combo) {
    s->fade_from = s->last_sample;
    s->fade_remaining = s->kind && s->last_sample ? 128 : 0;
    s->position = 0;
    s->step = 65536;
    s->kind = kind;
    if (kind == 5) {
        s->offset = 86372;
        s->length = 35176;
    } else if (kind == 1) {
        s->offset = 35176;
        s->length = 16055;
    } else if (kind == 2) {
        s->offset = 35176 + 16055;
        s->length = 25701;
        unsigned extra = combo > 1 ? combo - 1 : 0;
        if (extra > 6)
            extra = 6;
        s->step = 65536 + extra * 65536 / 10;
    } else if (kind == 3) {
        s->offset = 76932;
        s->length = 1440;
    } else if (kind == 4) {
        s->offset = 78372;
        s->length = 8000;
    } else {
        s->offset = 0;
        s->length = 0;
    }
}
static int sample(unsigned index) {
    return (int16_t)(jump_sounds[index * 2] | jump_sounds[index * 2 + 1] << 8);
}
void nabo_jump_sound_fill(nabo_jump_sound_t* s, int16_t* pcm, size_t frames) {
    memset(pcm, 0, frames * sizeof(*pcm));
    for (size_t i = 0; i < frames; ++i) {
        unsigned p = s->position >> 16;
        if (p >= s->length) {
            if (s->kind == 5) {
                s->position = 0;
                p = 0;
            } else if (!s->fade_remaining)
                break;
        }
        int a = p < s->length ? sample(s->offset + p) : 0;
        if (s->step == 65536)
            pcm[i] = (int16_t)a;
        else {
            int b = sample(s->offset + (p + 1 < s->length ? p + 1 : p));
            pcm[i] = (int16_t)(a + (b - a) * (int64_t)(s->position & 65535) / 65536);
        }
        if (s->fade_remaining) {
            unsigned remaining = s->fade_remaining--;
            pcm[i] =
                (int16_t)((s->fade_from * (int)remaining + pcm[i] * (int)(128 - remaining)) / 128);
        }
        s->position += s->step;
    }
    if (frames)
        s->last_sample = pcm[frames - 1];
}
