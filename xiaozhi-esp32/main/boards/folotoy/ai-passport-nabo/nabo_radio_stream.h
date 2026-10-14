#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { size_t interval, audio_left, metadata_left; bool length_next; } nabo_icy_t;
void nabo_icy_init(nabo_icy_t *s, size_t interval);
/* Output never exceeds input size; supports arbitrary network chunk splits. */
bool nabo_icy_feed(nabo_icy_t *s, const uint8_t *in, size_t bytes,
                   uint8_t *out, size_t capacity, size_t *written);
typedef struct { uint32_t step, position; int16_t previous; } nabo_radio_resampler_t;
bool nabo_radio_resampler_init(nabo_radio_resampler_t *s, unsigned rate);
/* n <= 1152, rate >= 8000; destination must hold n*2+2 samples. */
size_t nabo_radio_resample(nabo_radio_resampler_t *s, const int16_t *in,
                           size_t n, int16_t *out, size_t capacity);
