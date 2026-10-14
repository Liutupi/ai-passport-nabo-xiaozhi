#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Copies an expression request into one word; consumed by the UI task. */
bool muse_nabo_emote(const char *name, uint32_t duration_ms);
uint32_t muse_nabo_revision(void);
bool muse_nabo_dirty_area(int *x0, int *y0, int *x1, int *y1);
void muse_nabo_capture_arm(void);
void muse_nabo_capture_delta_request(void);
void muse_nabo_capture_delta_begin(void);
void muse_nabo_capture_attach(void *display);
void muse_nabo_chat_idle_tick(void);
void muse_nabo_request_tts_probe(void);
void muse_nabo_request_tts_probe_long(void);
