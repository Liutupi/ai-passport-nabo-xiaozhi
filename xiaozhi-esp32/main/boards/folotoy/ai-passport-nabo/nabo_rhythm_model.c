#include "nabo_rhythm_model.h"
#include <string.h>

/* Each phrase is one bar of eighth-note steps: {note mask, high-note mask}.
 * Level I quarters, II eighths, III long runs, IV off-beat syncopation. */
typedef struct {
    uint8_t mask, high;
} pattern_t;
#define PATTERNS 6
static const pattern_t echo_patterns[RH_LEVELS][PATTERNS] = {
    {{0x55, 0}, {0x51, 0}, {0x45, 0}, {0x15, 0}, {0x54, 0}, {0x11, 0}},
    {{0x5D, 0}, {0x17, 0}, {0x75, 0}, {0x59, 0}, {0x53, 0}, {0x4D, 0}},
    {{0x5F, 0}, {0x7D, 0}, {0x77, 0}, {0x1F, 0}, {0x7C, 0}, {0xCF, 0}},
    {{0x5A, 0}, {0x69, 0}, {0xA6, 0}, {0xC9, 0}, {0x49, 0}, {0x6A, 0}},
};
static const pattern_t duet_patterns[RH_LEVELS][PATTERNS] = {
    {{0x55, 0x44}, {0x55, 0x50}, {0x51, 0x10}, {0x45, 0x04}, {0x55, 0x14}, {0x15, 0x04}},
    {{0x5D, 0x0C}, {0x17, 0x02}, {0x75, 0x30}, {0x59, 0x18}, {0x4D, 0x08}, {0x53, 0x02}},
    {{0x5F, 0x0A}, {0x7D, 0x28}, {0x77, 0x22}, {0x1F, 0x15}, {0x7C, 0x54}, {0x57, 0x42}},
    {{0x5A, 0x0A}, {0x69, 0x28}, {0xA6, 0x84}, {0xC9, 0x48}, {0x49, 0x08}, {0x6A, 0x2A}},
};
static const uint16_t bpms[RH_MODES][RH_LEVELS] = {
    {90, 96, 104, 110}, {88, 94, 98, 104}, {108, 112, 116, 120}};

static uint32_t next_random(uint32_t* state) {
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

uint16_t nabo_rhythm_bpm(int mode, int level) {
    if (mode < 0 || mode >= RH_MODES || level < 0 || level >= RH_LEVELS)
        return 100;
    return bpms[mode][level];
}

void nabo_rhythm_start(nabo_rhythm_t* game, int mode, int level, uint32_t seed) {
    memset(game, 0, sizeof(*game));
    if (mode < 0 || mode >= RH_MODES)
        mode = 0;
    if (level < 0 || level >= RH_LEVELS)
        level = 0;
    game->mode = (uint8_t)mode;
    game->level = (uint8_t)level;
    game->bpm = bpms[mode][level];
    uint32_t state = seed ? seed : 1;
    uint8_t order[PATTERNS] = {0, 1, 2, 3, 4, 5};
    for (int i = PATTERNS - 1; i > 0; --i) {
        int j = (int)(next_random(&state) % (uint32_t)(i + 1));
        uint8_t t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    /* REMIX alternates single-key and two-key phrases; its level IV draws
     * every phrase from the hardest tables independently. */
    static const uint8_t remix_echo[RH_LEVELS] = {1, 1, 2, 3}, remix_duet[RH_LEVELS] = {0, 1, 2, 3};
    for (int p = 0; p < RH_PHRASES; ++p) {
        const pattern_t* pick;
        if (mode == RH_MODE_ECHO)
            pick = &echo_patterns[level][order[p]];
        else if (mode == RH_MODE_DUET)
            pick = &duet_patterns[level][order[p]];
        else {
            bool duet = level == 3 ? (next_random(&state) & 1) : (p & 1);
            int source = duet ? remix_duet[level] : remix_echo[level];
            pick = duet ? &duet_patterns[source][order[p]] : &echo_patterns[source][order[p]];
        }
        game->mask[p] = pick->mask;
        game->high[p] = pick->high & pick->mask;
        game->two_keys[p] = pick->high != 0;
        for (int s = 0; s < RH_STEPS; ++s)
            game->notes += (pick->mask >> s) & 1;
    }
}

uint32_t nabo_rhythm_step_sample(const nabo_rhythm_t* game, int global_step) {
    /* One eighth note lasts 30 / bpm seconds. */
    return (uint32_t)((uint64_t)global_step * RH_RATE * 30u / game->bpm);
}

uint32_t nabo_rhythm_length(const nabo_rhythm_t* game) {
    return nabo_rhythm_step_sample(game, RH_TOTAL_STEPS);
}

unsigned nabo_rhythm_events(const nabo_rhythm_t* game, int global_step) {
    if (global_step == RH_TOTAL_STEPS)
        return RH_EV_END;
    if (global_step < 0 || global_step > RH_TOTAL_STEPS)
        return 0;
    int bar = global_step / RH_STEPS, step = global_step % RH_STEPS;
    unsigned events = 0;
    if (!(step & 1))
        events |= RH_EV_BEAT;
    if (!step)
        events |= RH_EV_ACCENT;
    if (bar >= 2 && !(bar & 1) && !step)
        events |= RH_EV_TURN;
    if (bar >= 1 && (bar & 1)) {
        int phrase = (bar - 1) / 2;
        if ((game->mask[phrase] >> step) & 1)
            events |= (game->high[phrase] >> step) & 1 ? RH_EV_HIGH : RH_EV_LOW;
    }
    return events;
}

void nabo_rhythm_locate(const nabo_rhythm_t* game, uint32_t sample, rh_position_t* out) {
    uint32_t bar_samples = nabo_rhythm_step_sample(game, RH_STEPS);
    int bar = 0;
    while (bar < RH_BARS && nabo_rhythm_step_sample(game, (bar + 1) * RH_STEPS) <= sample)
        ++bar;
    out->bar = bar;
    if (bar >= RH_BARS) {
        out->stage = RH_STAGE_DONE;
        out->phrase = RH_PHRASES - 1;
        out->step = RH_STEPS - 1;
        out->beat = 3;
        out->bar_permille = 1000;
        return;
    }
    uint32_t into = sample - nabo_rhythm_step_sample(game, bar * RH_STEPS);
    out->bar_permille = (int)((uint64_t)into * 1000 / bar_samples);
    if (out->bar_permille > 999)
        out->bar_permille = 999;
    out->step = out->bar_permille * RH_STEPS / 1000;
    out->beat = out->step / 2;
    if (!bar) {
        out->stage = RH_STAGE_COUNTIN;
        out->phrase = -1;
    } else {
        out->stage = bar & 1 ? RH_STAGE_LISTEN : RH_STAGE_TURN;
        out->phrase = (bar - 1) / 2;
    }
}

static uint32_t note_sample(const nabo_rhythm_t* game, int phrase, int step) {
    return nabo_rhythm_step_sample(game, (2 + 2 * phrase) * RH_STEPS + step);
}

rh_tap_t nabo_rhythm_tap(nabo_rhythm_t* game, uint32_t sample, int lane) {
    rh_tap_t result = {RH_JUDGE_NONE, -1, -1, 0, false};
    int best_phrase = -1, best_step = -1;
    uint32_t best = UINT32_MAX;
    bool inside_turn = false;
    for (int p = 0; p < RH_PHRASES; ++p) {
        uint32_t start = note_sample(game, p, 0), end = note_sample(game, p, RH_STEPS);
        if (sample + RH_GOOD_SAMPLES >= start && sample < end)
            inside_turn = true;
        for (int s = 0; s < RH_STEPS; ++s) {
            if (!((game->mask[p] >> s) & 1) || game->judge[p][s] != RH_JUDGE_NONE)
                continue;
            uint32_t at = note_sample(game, p, s);
            uint32_t distance = sample > at ? sample - at : at - sample;
            if (distance < best) {
                best = distance;
                best_phrase = p;
                best_step = s;
            }
        }
    }
    if (best_phrase < 0 || best > RH_GOOD_SAMPLES) {
        /* Tapping along with the demonstration is allowed; stray taps in a
         * response bar cost accuracy. */
        if (inside_turn)
            ++game->extra;
        return result;
    }
    int want = (game->high[best_phrase] >> best_step) & 1;
    if (game->two_keys[best_phrase] && lane != want) {
        ++game->extra;
        result.wrong_key = true;
        result.phrase = best_phrase;
        result.step = best_step;
        return result;
    }
    int32_t delta = (int32_t)(sample - note_sample(game, best_phrase, best_step));
    result.phrase = best_phrase;
    result.step = best_step;
    result.offset_ms = delta * 1000 / RH_RATE;
    result.judge = best <= RH_PERFECT_SAMPLES ? RH_JUDGE_PERFECT : RH_JUDGE_GOOD;
    game->judge[best_phrase][best_step] = (uint8_t)result.judge;
    game->offset_ms[best_phrase][best_step] = (int16_t)result.offset_ms;
    game->tap_at[best_phrase][best_step] = sample;
    if (result.judge == RH_JUDGE_PERFECT)
        ++game->perfect;
    else
        ++game->good;
    return result;
}

int nabo_rhythm_update(nabo_rhythm_t* game, uint32_t sample) {
    int changed = 0;
    for (int p = 0; p < RH_PHRASES; ++p)
        for (int s = 0; s < RH_STEPS; ++s) {
            if (!((game->mask[p] >> s) & 1) || game->judge[p][s] != RH_JUDGE_NONE)
                continue;
            if (sample > note_sample(game, p, s) + RH_GOOD_SAMPLES) {
                game->judge[p][s] = RH_JUDGE_MISS;
                ++game->miss;
                ++changed;
            }
        }
    return changed;
}

int nabo_rhythm_percent(const nabo_rhythm_t* game) {
    if (!game->notes)
        return 0;
    int points = (game->perfect * 100 + game->good * 65) / game->notes - game->extra * 3;
    return points < 0 ? 0 : points > 100 ? 100 : points;
}

char nabo_rhythm_grade(int percent) {
    return percent >= 95 ? 'S' : percent >= 85 ? 'A' : percent >= 70 ? 'B' : percent >= 50 ? 'C' : 'D';
}
