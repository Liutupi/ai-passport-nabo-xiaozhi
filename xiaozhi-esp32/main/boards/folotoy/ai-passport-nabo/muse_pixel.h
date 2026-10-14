#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { MUSE_MODE_BOOT, MUSE_MODE_IDLE, MUSE_MODE_LISTENING, MUSE_MODE_THINKING, MUSE_MODE_SPEAKING, MUSE_MODE_ERROR, MUSE_MODE_OFF, MUSE_MODE_COUNT } muse_mode_t;
typedef struct { muse_mode_t mode; float t, mode_t, level, happy; } muse_pose_t;
void muse_pixel_set_size(int px);
void muse_pixel_render(const muse_pose_t *pose);
void muse_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1);
uint32_t muse_nabo_revision(void);
bool muse_nabo_emote(const char *name, uint32_t duration_ms);
#ifdef __cplusplus
}
#endif
