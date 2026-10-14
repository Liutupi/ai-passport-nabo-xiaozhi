#pragma once
#include <stdbool.h>
#include <stdint.h>

#define NABO_RADIO_MAX_SAMPLES_PER_FRAME 2304

typedef struct { void *handle; } nabo_radio_decoder_t;
typedef struct {
    int frame_bytes;
    int channels;
    int hz;
    int bitrate_kbps;
} nabo_radio_frame_info_t;

/* Initialize the structure to {0}; init/free own the Helix heap allocations. */
bool nabo_radio_decoder_init(nabo_radio_decoder_t *decoder);
void nabo_radio_decoder_free(nabo_radio_decoder_t *decoder);
/* Returns samples per channel. frame_bytes includes skipped leading bytes.
 * Zero frame_bytes means retain input and read more. A reservoir-underflow
 * frame is consumed with a zero return value. PCM holds 2304 int16_t values.
 */
int nabo_radio_decoder_decode(nabo_radio_decoder_t *decoder, const uint8_t *input,
                              int bytes, int16_t *pcm, nabo_radio_frame_info_t *info);
