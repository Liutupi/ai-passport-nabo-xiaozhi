#pragma once
/* Call-and-response rhythm game ("节奏回声 ECHO CLUB"): pure timing model.
 * All song times are 16 kHz sample indices so audio, drawing and input share
 * one clock. No heap, no ESP-IDF dependency; exercised by test_rhythm.py. */
#include <stdbool.h>
#include <stdint.h>

#define RH_RATE 16000
#define RH_MODES 3
#define RH_LEVELS 4
#define RH_PHRASES 4
#define RH_STEPS 8                             /* eighth notes per 4/4 bar */
#define RH_BARS (1 + 2 * RH_PHRASES)           /* count-in, then listen/turn pairs */
#define RH_TOTAL_STEPS (RH_BARS * RH_STEPS)
#define RH_PERFECT_SAMPLES (60 * RH_RATE / 1000)
#define RH_GOOD_SAMPLES (125 * RH_RATE / 1000)

typedef enum { RH_MODE_ECHO, RH_MODE_DUET, RH_MODE_REMIX } rh_mode_t;
typedef enum { RH_STAGE_COUNTIN, RH_STAGE_LISTEN, RH_STAGE_TURN, RH_STAGE_DONE } rh_stage_t;
typedef enum { RH_JUDGE_NONE, RH_JUDGE_PERFECT, RH_JUDGE_GOOD, RH_JUDGE_MISS } rh_judge_t;

/* Audio events per step (bit flags). */
#define RH_EV_BEAT 1u
#define RH_EV_ACCENT 2u
#define RH_EV_LOW 4u
#define RH_EV_HIGH 8u
#define RH_EV_TURN 16u
#define RH_EV_END 32u

typedef struct {
    uint8_t mode, level;
    uint16_t bpm;
    bool two_keys[RH_PHRASES];        /* phrase uses the DOWN key for high notes */
    uint8_t mask[RH_PHRASES];         /* bit s: note on eighth step s */
    uint8_t high[RH_PHRASES];         /* bit s: that note is high (DOWN) */
    uint8_t judge[RH_PHRASES][RH_STEPS];
    int16_t offset_ms[RH_PHRASES][RH_STEPS];
    uint32_t tap_at[RH_PHRASES][RH_STEPS];
    uint16_t notes, perfect, good, miss, extra;
} nabo_rhythm_t;

typedef struct {
    rh_stage_t stage;
    int phrase;   /* -1 during count-in */
    int bar;      /* 0..RH_BARS */
    int step;     /* 0..7 within the bar */
    int beat;     /* 0..3 within the bar */
    int bar_permille;
} rh_position_t;

typedef struct {
    rh_judge_t judge;
    int phrase, step, offset_ms;
    bool wrong_key;
} rh_tap_t;

uint16_t nabo_rhythm_bpm(int mode, int level);
void nabo_rhythm_start(nabo_rhythm_t* game, int mode, int level, uint32_t seed);
uint32_t nabo_rhythm_step_sample(const nabo_rhythm_t* game, int global_step);
uint32_t nabo_rhythm_length(const nabo_rhythm_t* game);
unsigned nabo_rhythm_events(const nabo_rhythm_t* game, int global_step);
void nabo_rhythm_locate(const nabo_rhythm_t* game, uint32_t sample, rh_position_t* out);
rh_tap_t nabo_rhythm_tap(nabo_rhythm_t* game, uint32_t sample, int lane);
/* Marks overdue notes as missed; returns how many changed. */
int nabo_rhythm_update(nabo_rhythm_t* game, uint32_t sample);
int nabo_rhythm_percent(const nabo_rhythm_t* game);
char nabo_rhythm_grade(int percent);
