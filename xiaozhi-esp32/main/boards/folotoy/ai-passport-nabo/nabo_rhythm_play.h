#pragma once
/* Song clock shared by the nabo_apps audio worker and the LVGL task.
 * The worker owns the synthesizer; the UI owns the game and only posts
 * sound requests through a single-producer queue. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "nabo_rhythm_model.h"
#include "nabo_rhythm_sound.h"

/* UI task. */
void nabo_rhythm_play_start(const nabo_rhythm_t* game);
void nabo_rhythm_play_stop(void);
bool nabo_rhythm_play_running(void);
/* Song position (16 kHz samples) at an esp_timer time; false before audio starts. */
bool nabo_rhythm_play_sample_at(int64_t time_us, int64_t* sample);
void nabo_rhythm_play_sound(rh_sound_t sound);
void nabo_rhythm_play_set_latency_ms(int latency_ms);
int nabo_rhythm_play_latency_ms(void);

/* Audio worker: renders exactly `frames` mono 16 kHz samples. */
void nabo_rhythm_audio_fill(int16_t* pcm, size_t frames);
