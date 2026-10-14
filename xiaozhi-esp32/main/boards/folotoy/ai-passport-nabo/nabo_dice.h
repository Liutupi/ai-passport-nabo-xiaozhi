#pragma once
#include <stdbool.h>
#include <stdint.h>

/* UI-safe intent and roll hand-off. No NVS, network or I2S work runs here. */
void nabo_dice_open(void);
void nabo_dice_close(void);
bool nabo_dice_wanted(void);
uint32_t nabo_dice_new_seed(void);
/* The UI publishes each throw; the voice task rebuilds it from the seed and
 * synthesizes the matching clatter. A newer throw replaces an unplayed one. */
void nabo_dice_thrown(uint32_t seed, unsigned count, int64_t at_us);
/* The UI reports the settled faces for the USB status line. */
void nabo_dice_settled(const uint8_t *values, unsigned count);
unsigned nabo_dice_status(char *out, unsigned cap);

/* Voice-owner only. */
bool nabo_dice_take_throw(uint32_t *seed, unsigned *count, int64_t *at_us);
