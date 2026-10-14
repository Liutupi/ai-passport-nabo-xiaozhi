#pragma once
#include <stdbool.h>
#include <stdint.h>

/* UI-safe intent/count snapshots. No NVS, network or audio work runs here. */
void nabo_muyu_open(void);
void nabo_muyu_close(void);
bool nabo_muyu_wanted(void);
uint64_t nabo_muyu_count(void);
/* Each physical DOWN edge calls once. Returns false while the page is closed.
 * Every accepted hit counts; at most one further sound is pending. */
bool nabo_muyu_hit(void);
bool nabo_muyu_sound_enabled(void);
void nabo_muyu_set_sound(bool enabled);

/* Voice-owner-only: load once, take a coalesced sound, debounce writes, or save
 * on exit. Audio itself remains on muse_voice's existing sole I2S writer. */
void nabo_muyu_init(void);
bool nabo_muyu_take_hit(void);
void nabo_muyu_idle_tick(void);
void nabo_muyu_save(void);
