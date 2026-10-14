#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decode a native-size inclusive horizontal slice into caller-owned pixels.
 * Reads palette/RLE directly from Flash. No allocation, palette cache, or
 * frame/row scratch storage. Destination may be a line in an LCD strip.
 */
bool nabo_muyu_draw_row(unsigned frame, unsigned y, int x0, int x1,
                         uint16_t *dst, size_t capacity);
