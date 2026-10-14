/* Offline dice: intent and throw hand-off between the LVGL page and the sole
 * I2S writer (muse_voice). Nothing here touches Flash, network or audio. */
#include "nabo_dice.h"
#include <stdio.h>
#include "esp_random.h"
#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_wanted, s_pending;
static uint32_t s_seed, s_rolls;
static uint8_t s_count, s_last[3], s_last_count;
static int64_t s_at;

void nabo_dice_open(void)
{
    portENTER_CRITICAL(&s_lock);
    s_wanted = true;
    s_pending = false;
    portEXIT_CRITICAL(&s_lock);
}

void nabo_dice_close(void)
{
    portENTER_CRITICAL(&s_lock);
    s_wanted = false;
    s_pending = false;
    portEXIT_CRITICAL(&s_lock);
}

bool nabo_dice_wanted(void)
{
    portENTER_CRITICAL(&s_lock);
    bool wanted = s_wanted;
    portEXIT_CRITICAL(&s_lock);
    return wanted;
}

uint32_t nabo_dice_new_seed(void)
{
    uint32_t seed;
    do seed = esp_random(); while (!seed);
    return seed;
}

void nabo_dice_thrown(uint32_t seed, unsigned count, int64_t at_us)
{
    portENTER_CRITICAL(&s_lock);
    if (s_wanted) {
        s_seed = seed;
        s_count = (uint8_t)count;
        s_at = at_us;
        s_pending = true;
        ++s_rolls;
    }
    portEXIT_CRITICAL(&s_lock);
}

bool nabo_dice_take_throw(uint32_t *seed, unsigned *count, int64_t *at_us)
{
    portENTER_CRITICAL(&s_lock);
    bool pending = s_wanted && s_pending;
    s_pending = false;
    *seed = s_seed;
    *count = s_count;
    *at_us = s_at;
    portEXIT_CRITICAL(&s_lock);
    return pending;
}

void nabo_dice_settled(const uint8_t *values, unsigned count)
{
    if (count > 3) count = 3;
    portENTER_CRITICAL(&s_lock);
    for (unsigned i = 0; i < count; ++i) s_last[i] = values[i];
    s_last_count = (uint8_t)count;
    portEXIT_CRITICAL(&s_lock);
}

unsigned nabo_dice_status(char *out, unsigned cap)
{
    portENTER_CRITICAL(&s_lock);
    bool wanted = s_wanted;
    uint32_t rolls = s_rolls;
    uint8_t last[3] = {s_last[0], s_last[1], s_last[2]}, n = s_last_count;
    portEXIT_CRITICAL(&s_lock);
    int w = snprintf(out, cap, "@dice {\"wanted\":%s,\"rolls\":%lu,\"last\":[",
                     wanted ? "true" : "false", (unsigned long)rolls);
    for (unsigned i = 0; i < n && w > 0 && (unsigned)w < cap; ++i)
        w += snprintf(out + w, cap - (unsigned)w, i ? ",%u" : "%u", last[i]);
    if (w > 0 && (unsigned)w < cap) w += snprintf(out + w, cap - (unsigned)w, "]}");
    return w > 0 ? (unsigned)w : 0;
}
