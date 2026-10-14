/* Generated artwork is streamed directly from Flash into the LCD strip. */
#include "nabo_muyu_codec.h"
#include "nabo_muyu_assets.h"

extern const unsigned char muyu_blob[] asm("_binary_muyu_rgb565_rle_bin_start");

static uint32_t read32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool nabo_muyu_draw_row(unsigned frame, unsigned y, int x0, int x1,
                         uint16_t *dst, size_t capacity)
{
    if (!dst || frame >= NABO_MUYU_FRAME_COUNT || y >= NABO_MUYU_HEIGHT ||
        x0 < 0 || x1 < x0 || x1 >= NABO_MUYU_WIDTH || capacity < (size_t)(x1 - x0 + 1)) return false;
    const nabo_muyu_frame_t *item = &nabo_muyu_frames[frame];
    const size_t bytes = NABO_MUYU_BLOB_BYTES;
    if (bytes < 512 || item->palette > bytes - 512 ||
        bytes < (NABO_MUYU_HEIGHT + 1) * 4u ||
        item->rows > bytes - (NABO_MUYU_HEIGHT + 1) * 4u || item->rle > bytes) return false;
    uint32_t position = read32(muyu_blob + item->rows + y * 4u);
    uint32_t end = read32(muyu_blob + item->rows + (y + 1) * 4u);
    if (position > end || end > bytes - item->rle) return false;
    unsigned source_x = 0;
    while (position < end) {
        unsigned token = muyu_blob[item->rle + position++];
        bool repeat = (token & 128u) != 0;
        unsigned run = (token & 127u) + (repeat ? 2u : 1u);
        unsigned packet = repeat ? 1u : run;
        if (run > NABO_MUYU_WIDTH - source_x || packet > end - position) return false;
        unsigned first = source_x > (unsigned)x0 ? source_x : (unsigned)x0;
        unsigned limit = source_x + run - 1u < (unsigned)x1 ? source_x + run - 1u : (unsigned)x1;
        for (unsigned x = first; x <= limit; ++x) {
            unsigned index = muyu_blob[item->rle + position + (repeat ? 0u : x - source_x)];
            const unsigned char *color = muyu_blob + item->palette + index * 2u;
            dst[x - (unsigned)x0] = (uint16_t)color[0] | (uint16_t)color[1] << 8;
        }
        source_x += run;
        position += packet;
    }
    return source_x == NABO_MUYU_WIDTH;
}
