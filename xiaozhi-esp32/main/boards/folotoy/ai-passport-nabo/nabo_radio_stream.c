#include "nabo_radio_stream.h"
#include <string.h>

void nabo_icy_init(nabo_icy_t *s, size_t interval)
{
    *s = (nabo_icy_t){.interval=interval, .audio_left=interval};
}

bool nabo_icy_feed(nabo_icy_t *s, const uint8_t *in, size_t bytes,
                   uint8_t *out, size_t capacity, size_t *written)
{
    if (!s || !written || (!in && bytes) || (!out && bytes) || capacity < bytes) return false;
    *written = 0;
    if (!s->interval) { if (bytes) memcpy(out,in,bytes); *written=bytes; return true; }
    while (bytes) {
        if (s->metadata_left) {
            size_t take = bytes < s->metadata_left ? bytes : s->metadata_left;
            in += take; bytes -= take; s->metadata_left -= take;
        } else if (!s->audio_left) {
            s->metadata_left = (size_t)*in++ * 16;
            --bytes;
            s->audio_left = s->interval;
        } else {
            size_t take = bytes < s->audio_left ? bytes : s->audio_left;
            memcpy(out+*written,in,take);
            *written += take; in += take; bytes -= take; s->audio_left -= take;
        }
    }
    return true;
}

bool nabo_radio_resampler_init(nabo_radio_resampler_t *s, unsigned rate)
{
    if (!s || rate < 8000 || rate > 48000) return false;
    *s = (nabo_radio_resampler_t){.step=(uint32_t)(((uint64_t)rate<<16)/16000)};
    return true;
}

size_t nabo_radio_resample(nabo_radio_resampler_t *s, const int16_t *in,
                           size_t n, int16_t *out, size_t capacity)
{
    if (!s || !s->step || !in || !out || !n || n>1152 || capacity<n*2+2) return 0;
    size_t written=0;
    while ((s->position>>16)<n) {
        size_t at=s->position>>16;
        int32_t a=at ? in[at-1] : s->previous, b=in[at];
        out[written++]=(int16_t)(a+(((int64_t)(b-a)*(s->position&65535))>>16));
        s->position+=s->step;
    }
    s->position-=(uint32_t)n<<16;
    s->previous=in[n-1];
    return written;
}
