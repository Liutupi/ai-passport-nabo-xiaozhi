/* NABO artwork belongs to the user. Streaming renderer for Muse Gadget SDK. */
#include "muse_pixel.h"
#include "muse_nabo.h"
#include "nabo_assets.h"
#include "nabo_dirty.h"
#include <math.h>

extern const unsigned char nabo_blob[] asm("_binary_nabo_rgb565_rle_bin_start");
static int s_px = NABO_WIDTH;
static int s_frame;
static int s_previous_frame;
static unsigned s_blink, s_previous_blink;
static bool s_full_damage = true;
static uint32_t s_revision = 1;
static uint32_t s_request;
static uint16_t s_palette[256];
static int s_palette_frame = -1;
static uint16_t s_eye_palette[256];
static int s_eye_palette_frame = -1;
static int s_emote = -1;
static float s_emote_started, s_emote_until;

static uint32_t read32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool same(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

bool muse_nabo_emote(const char *name, uint32_t duration_ms)
{
    if (duration_ms < 1000 || duration_ms > 30000) return false;
    for (unsigned i = 0; i < NABO_CLIP_COUNT; ++i) {
        if (same(name, nabo_clips[i].name)) {
            __atomic_store_n(&s_request, (duration_ms << 8) | (i + 1), __ATOMIC_RELEASE);
            return true;
        }
    }
    return false;
}

uint32_t muse_nabo_revision(void) { return s_revision; }

bool muse_nabo_dirty_area(int *x0, int *y0, int *x1, int *y1)
{
    if (s_full_damage) {
        s_full_damage = false;
        *x0 = *y0 = 0; *x1 = *y1 = s_px - 1;
        return true;
    }
    const uint8_t *box = nabo_dirty[s_previous_frame][s_frame];
    int a = box[0], b = box[1], c = box[2], d = box[3];
    if (s_blink || s_previous_blink) {
        /* Include the old overlay when returning to open eyes or another mode. */
        if (a > c) { a = 72; b = 104; c = 154; d = 137; }
        else {
            if (a > 72) a = 72;
            if (b > 104) b = 104;
            if (c < 154) c = 154;
            if (d < 137) d = 137;
        }
    }
    if (a > c) return false;
    *x0 = a * s_px / NABO_WIDTH;
    *y0 = b * s_px / NABO_HEIGHT;
    *x1 = ((c+1)*s_px+NABO_WIDTH-1)/NABO_WIDTH-1;
    *y1 = ((d+1)*s_px+NABO_HEIGHT-1)/NABO_HEIGHT-1;
    return true;
}

static unsigned idle_blink(float elapsed)
{
    /* Existing half-/closed-eye art, with a long neutral hold between blinks.
     * Use clip time so entering idle always starts with open eyes. */
    static const float waits[] = {4.3f, 6.1f, 5.2f, 7.0f, 4.8f};
    float phase = fmodf(elapsed, 28.8f);
    for (unsigned i = 0; i < sizeof(waits) / sizeof(waits[0]); ++i) {
        if (phase < waits[i]) return 0;
        phase -= waits[i];
        if (phase < 0.08f) return 1;
        if (phase < 0.20f) return 2;
        if (phase < 0.28f) return 1;
        phase -= 0.28f;
    }
    return 0;
}

uint32_t muse_pixel_accent(muse_mode_t mode)
{
    static const uint32_t colors[] = {0xf4bf67, 0xe7b9cf, 0x7adbc1, 0xbeb0f0, 0x9fd4f5, 0xe98d9c, 0x686880};
    return colors[(unsigned)mode < MUSE_MODE_COUNT ? mode : MUSE_MODE_IDLE];
}

void muse_pixel_set_size(int px)
{
    if (px < 1) px = 1;
    if (px > 512) px = 512;
    if (s_px != px) { s_px = px; s_full_damage = true; ++s_revision; }
}

void muse_pixel_render(const muse_pose_t *pose)
{
    uint32_t request = __atomic_exchange_n(&s_request, 0, __ATOMIC_ACQUIRE);
    if (request) {
        s_emote = (int)(request & 255) - 1;
        s_emote_started = pose->t;
        s_emote_until = pose->t + (float)(request >> 8) / 1000.0f;
    }
    unsigned mode = (unsigned)pose->mode < MUSE_MODE_COUNT ? pose->mode : MUSE_MODE_IDLE;
    int clip_id = nabo_mode_clips[mode];
    float elapsed = pose->mode_t;
    /* Preserve recording and error feedback while a Muse expression is queued. */
    if (pose->t < s_emote_until && mode != MUSE_MODE_LISTENING && mode != MUSE_MODE_ERROR && mode != MUSE_MODE_OFF) {
        clip_id = s_emote;
        elapsed = pose->t - s_emote_started;
    } else if (pose->happy > 0.05f && mode == MUSE_MODE_IDLE) {
        clip_id = NABO_CLIP_HAPPY;
    }
    const nabo_clip_t *clip = &nabo_clips[clip_id];
    if (!(elapsed >= 0.0f)) elapsed = 0; /* also handles NaN */
    /* Clamp before conversion, then use modulo to avoid an overflowing cast. */
    if (elapsed > 100000000.0f) elapsed = 100000000.0f;
    unsigned index = (unsigned)(elapsed * clip->fps);
    if (clip->loop) index %= clip->count;
    else if (index >= clip->count) index = clip->count - 1;
    int next = clip->frames[index];
    unsigned blink = mode == MUSE_MODE_IDLE && clip_id == nabo_mode_clips[MUSE_MODE_IDLE]
                     && next == NABO_FRAME_IDLE ? idle_blink(elapsed) : 0;
    if (next != s_frame || blink != s_blink) {
        s_previous_frame = s_frame;
        s_previous_blink = s_blink;
        s_frame = next;
        s_blink = blink;
        ++s_revision;
    }
}

static uint16_t eye_blend(uint16_t base, uint16_t eye, unsigned alpha)
{
    if (alpha == 255) return eye;
    unsigned r = (((base >> 11) + (eye >> 11)) + 1) / 2;
    unsigned g = ((((base >> 5) & 63) + ((eye >> 5) & 63)) + 1) / 2;
    unsigned b = (((base & 31) + (eye & 31)) + 1) / 2;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void blink_eye(uint16_t *dst, int stride, int x0, int x1, int y0, int y1,
                      const nabo_frame_t *frame, int cx, int cy, int rx, int ry)
{
    int left = (cx-rx)*s_px/NABO_WIDTH;
    int right = ((cx+rx+1)*s_px+NABO_WIDTH-1)/NABO_WIDTH-1;
    if (left < x0) left = x0;
    if (right > x1) right = x1;
    if (left > right) return;
    for (int y = y0; y <= y1; ++y) {
        int sy = y * NABO_HEIGHT / s_px;
        int dy = sy - cy;
        if (dy < -ry || dy > ry) continue;
        uint32_t pos = read32(nabo_blob + frame->rows + sy * 4);
        uint32_t end = read32(nabo_blob + frame->rows + (sy+1) * 4);
        int source_x = 0, x = left;
        while (pos < end && x <= right) {
            unsigned token = nabo_blob[frame->rle + pos++];
            bool repeat = (token & 128) != 0;
            int run = (token & 127) + (repeat ? 2 : 1);
            while (x <= right && x * NABO_WIDTH / s_px < source_x + run) {
                int sx = x * NABO_WIDTH / s_px;
                int dx = sx - cx;
                if (dx*dx*ry*ry + dy*dy*rx*rx <= rx*rx*ry*ry) {
                    unsigned at = nabo_blob[frame->rle + pos + (repeat ? 0 : sx-source_x)];
                    unsigned alpha = dx*dx*(ry-1)*(ry-1) + dy*dy*(rx-1)*(rx-1)
                                     <= (rx-1)*(rx-1)*(ry-1)*(ry-1) ? 255 : 128;
                    uint16_t *pixel = dst + (y-y0)*stride + x-x0;
                    *pixel = eye_blend(*pixel, s_eye_palette[at], alpha);
                }
                ++x;
            }
            pos += repeat ? 1 : run;
            source_x += run;
        }
    }
}

static void blink_overlay(uint16_t *dst, int stride, int x0, int x1, int y0, int y1)
{
    if (!s_blink) return;
    int index = s_blink == 1 ? NABO_FRAME_TIRED : NABO_FRAME_SLEEP;
    const nabo_frame_t *frame = &nabo_frames[index];
    if (index != s_eye_palette_frame) {
        for (int i = 0; i < 256; ++i) {
            const unsigned char *p = nabo_blob + frame->palette + i*2;
            s_eye_palette[i] = (uint16_t)p[0] | (uint16_t)p[1] << 8;
        }
        s_eye_palette_frame = index;
    }
    blink_eye(dst, stride, x0, x1, y0, y1, frame, 89, 123, 17, 14);
    blink_eye(dst, stride, x0, x1, y0, y1, frame, 136, 118, 18, 14);
}

/* One RLE row is read directly from flash. No full frame or row allocation. */
void muse_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1)
{
    if (!dst || stride_px < x1 - x0 + 1 || x0 < 0 || y0 < 0 || x1 >= s_px || y1 >= s_px || x1 < x0 || y1 < y0) return;
    const nabo_frame_t *frame = &nabo_frames[s_frame];
    if (s_palette_frame != s_frame) {
        for (int i = 0; i < 256; ++i) {
            const unsigned char *p = nabo_blob + frame->palette + i * 2;
            s_palette[i] = (uint16_t)p[0] | (uint16_t)p[1] << 8;
        }
        s_palette_frame = s_frame;
    }
    if (s_px == NABO_WIDTH) {
        /* Native portrait: decode whole RLE runs, with no per-pixel division. */
        for (int y = y0; y <= y1; ++y) {
            uint32_t pos = read32(nabo_blob + frame->rows + y * 4);
            uint32_t end = read32(nabo_blob + frame->rows + (y + 1) * 4);
            int source_x = 0;
            uint16_t *row = dst + (y - y0) * stride_px;
            while (pos < end && source_x <= x1) {
                unsigned token = nabo_blob[frame->rle + pos++];
                bool repeat = (token & 128) != 0;
                int run = (token & 127) + (repeat ? 2 : 1);
                int first = source_x > x0 ? source_x : x0;
                int last = source_x + run - 1 < x1 ? source_x + run - 1 : x1;
                if (repeat) {
                    uint16_t color = s_palette[nabo_blob[frame->rle + pos]];
                    for (int x = first; x <= last; ++x) row[x - x0] = color;
                } else {
                    for (int x = first; x <= last; ++x)
                        row[x - x0] = s_palette[nabo_blob[frame->rle + pos + x - source_x]];
                }
                pos += repeat ? 1 : run;
                source_x += run;
            }
        }
        blink_overlay(dst, stride_px, x0, x1, y0, y1);
        return;
    }
    for (int y = y0; y <= y1; ++y) {
        unsigned sy = (unsigned)y * NABO_HEIGHT / s_px;
        uint32_t start = read32(nabo_blob + frame->rows + sy * 4);
        uint32_t end = read32(nabo_blob + frame->rows + (sy + 1) * 4);
        unsigned source_x = 0;
        int x = x0;
        for (uint32_t pos = start; pos < end && x <= x1;) {
            unsigned token = nabo_blob[frame->rle + pos++];
            unsigned repeat = token & 128;
            unsigned run = (token & 127) + (repeat ? 2 : 1);
            unsigned run_end = source_x + run;
            while (x <= x1 && (unsigned)x * NABO_WIDTH / s_px < run_end) {
                unsigned sx = (unsigned)x * NABO_WIDTH / s_px;
                unsigned color = nabo_blob[frame->rle + pos + (repeat ? 0 : sx - source_x)];
                dst[(y - y0) * stride_px + x - x0] = s_palette[color];
                ++x;
            }
            pos += repeat ? 1 : run;
            source_x = run_end;
        }
    }
    blink_overlay(dst, stride_px, x0, x1, y0, y1);
}
