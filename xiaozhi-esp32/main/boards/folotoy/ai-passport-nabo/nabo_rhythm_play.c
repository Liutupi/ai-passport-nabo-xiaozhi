#include "nabo_rhythm_play.h"
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define QUEUE_SIZE 16

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static nabo_rhythm_t s_pending;            /* written by UI before a new generation */
static volatile uint32_t s_generation;     /* UI increments to (re)start a song */
static volatile bool s_running;
static uint8_t s_queue[QUEUE_SIZE];
static volatile uint32_t s_queue_head, s_queue_tail;
/* About four 160-frame I2S DMA descriptors are queued ahead of each block. */
static volatile int s_latency_us = 42000;

/* Worker-owned state. */
static nabo_rhythm_sound_t s_synth;
static nabo_rhythm_t s_song;
static uint32_t s_seen_generation, s_song_sample;
static int s_next_step;
static bool s_song_active;

/* Shared clock anchor, guarded by s_lock. */
static bool s_anchor_valid;
static uint32_t s_anchor_sample;
static int64_t s_anchor_us;

void nabo_rhythm_play_start(const nabo_rhythm_t* game) {
    taskENTER_CRITICAL(&s_lock);
    s_pending = *game;
    s_anchor_valid = false;
    s_running = true;
    ++s_generation;
    taskEXIT_CRITICAL(&s_lock);
}

void nabo_rhythm_play_stop(void) {
    taskENTER_CRITICAL(&s_lock);
    s_running = false;
    s_anchor_valid = false;
    taskEXIT_CRITICAL(&s_lock);
}

bool nabo_rhythm_play_running(void) { return s_running; }

bool nabo_rhythm_play_sample_at(int64_t time_us, int64_t* sample) {
    taskENTER_CRITICAL(&s_lock);
    bool valid = s_anchor_valid;
    uint32_t base = s_anchor_sample;
    int64_t at = s_anchor_us;
    taskEXIT_CRITICAL(&s_lock);
    if (!valid)
        return false;
    *sample = (int64_t)base + (time_us - at) * RH_RATE / 1000000;
    return true;
}

void nabo_rhythm_play_sound(rh_sound_t sound) {
    uint32_t head = s_queue_head;
    if (head - s_queue_tail >= QUEUE_SIZE)
        return; /* drop rather than block the UI */
    s_queue[head % QUEUE_SIZE] = (uint8_t)sound;
    __atomic_store_n(&s_queue_head, head + 1, __ATOMIC_RELEASE);
}

void nabo_rhythm_play_set_latency_ms(int latency_ms) {
    if (latency_ms >= 0 && latency_ms <= 300)
        s_latency_us = latency_ms * 1000;
}

int nabo_rhythm_play_latency_ms(void) { return s_latency_us / 1000; }

static void schedule_steps(size_t frames) {
    uint32_t end = s_song_sample + (uint32_t)frames;
    while (s_next_step <= RH_TOTAL_STEPS) {
        uint32_t at = nabo_rhythm_step_sample(&s_song, s_next_step);
        if (at >= end)
            break;
        uint32_t delay = at > s_song_sample ? at - s_song_sample : 0;
        unsigned events = nabo_rhythm_events(&s_song, s_next_step);
        bool response_bar = (s_next_step / RH_STEPS) >= 2 && !((s_next_step / RH_STEPS) & 1);
        if (events & RH_EV_TURN)
            nabo_rhythm_sound_play(&s_synth, RH_SND_TURN, delay);
        else if (events & RH_EV_ACCENT)
            nabo_rhythm_sound_play(&s_synth, response_bar ? RH_SND_TICK : RH_SND_ACCENT, delay);
        else if (events & RH_EV_BEAT)
            nabo_rhythm_sound_play(&s_synth, response_bar ? RH_SND_SOFT_TICK : RH_SND_TICK, delay);
        if (events & RH_EV_LOW)
            nabo_rhythm_sound_play(&s_synth, RH_SND_LOW, delay);
        if (events & RH_EV_HIGH)
            nabo_rhythm_sound_play(&s_synth, RH_SND_HIGH, delay);
        ++s_next_step;
    }
}

void nabo_rhythm_audio_fill(int16_t* pcm, size_t frames) {
    uint32_t tail = s_queue_tail, head = __atomic_load_n(&s_queue_head, __ATOMIC_ACQUIRE);
    for (; tail != head; ++tail)
        nabo_rhythm_sound_play(&s_synth, (rh_sound_t)s_queue[tail % QUEUE_SIZE], 0);
    s_queue_tail = tail;

    taskENTER_CRITICAL(&s_lock);
    bool running = s_running;
    uint32_t generation = s_generation;
    if (running && generation != s_seen_generation) {
        s_song = s_pending;
        s_seen_generation = generation;
        s_song_sample = 0;
        s_next_step = 0;
        s_song_active = true;
    }
    if (!running)
        s_song_active = false;
    taskEXIT_CRITICAL(&s_lock);

    if (s_song_active)
        schedule_steps(frames);
    nabo_rhythm_sound_fill(&s_synth, pcm, frames);
    if (!s_song_active)
        return;
    int64_t audible = esp_timer_get_time() + s_latency_us;
    taskENTER_CRITICAL(&s_lock);
    if (s_anchor_valid && s_song_sample > s_anchor_sample) {
        /* The blocking I2S write paces this loop; smooth scheduler jitter so
         * the playhead and hit timing do not wobble block to block. */
        int64_t predicted =
            s_anchor_us + (int64_t)(s_song_sample - s_anchor_sample) * 1000000 / RH_RATE;
        int64_t error = audible - predicted;
        if (error > -15000 && error < 15000)
            audible = predicted + error / 8;
    }
    if (s_running && s_seen_generation == s_generation) {
        s_anchor_valid = true;
        s_anchor_sample = s_song_sample;
        s_anchor_us = audible;
    }
    taskEXIT_CRITICAL(&s_lock);
    s_song_sample += (uint32_t)frames;
}
