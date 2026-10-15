#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { NABO_KEYS_MAC, NABO_KEYS_WINDOWS, NABO_KEYS_OS_COUNT } nabo_keys_os_t;
/* Only high-frequency actions. Enum order equals menu order: each group is a
 * contiguous range. The value is a persistent index into NVS "maps_v2". */
typedef enum {
    /* 鼠标与翻页 */
    NABO_KEY_SCROLL_UP, NABO_KEY_SCROLL_DOWN, NABO_KEY_BACK, NABO_KEY_CLICK, NABO_KEY_RIGHT_CLICK,
    /* 常用操作 */
    NABO_KEY_ENTER, NABO_KEY_ESCAPE, NABO_KEY_COPY, NABO_KEY_PASTE, NABO_KEY_UNDO,
    NABO_KEY_SWITCH, NABO_KEY_DESKTOP, NABO_KEY_SCREEN_AREA, NABO_KEY_LOCK,
    /* 影音播放 */
    NABO_KEY_PLAY, NABO_KEY_SEEK_BACK, NABO_KEY_SEEK_FORWARD, NABO_KEY_VOL_UP,
    NABO_KEY_VOL_DOWN, NABO_KEY_MUTE, NABO_KEY_PREVIOUS, NABO_KEY_NEXT,
    /* Typeless 与 AI */
    NABO_KEY_DICTATE, NABO_KEY_TRANSLATE, NABO_KEY_ASK,
    NABO_KEY_COUNT
} nabo_key_t;
/* Report 1: standard eight-byte keyboard report. Report 2: little-endian
 * 16-bit Consumer usage. Report 3: mouse {buttons, x, y, wheel}. None of the
 * payloads include the report ID (it is in the GATT Report Reference). */
typedef struct { uint8_t report, len, data[8]; } nabo_key_packet_t;
enum { NABO_KEYS_GROUPS = 4, NABO_KEYS_V1_COUNT = 34, NABO_SCROLL_STEP = 3 };
bool nabo_keys_packet(nabo_keys_os_t os, nabo_key_t key, nabo_key_packet_t *out);
bool nabo_keys_override_packet(uint16_t encoded, nabo_key_packet_t *out);
/* Flip the wheel direction of a scroll packet (user "reverse" setting). */
void nabo_keys_reverse_scroll(nabo_key_packet_t *p);
/* v1.0-v1.2 stored 34 actions; maps a v1 index to this table or COUNT. */
nabo_key_t nabo_keys_from_v1(int v1_index);
const char *nabo_key_name(nabo_key_t key);
const char *nabo_keys_group_name(int group);
int nabo_keys_group_count(int group);
nabo_key_t nabo_keys_group_item(int group, int row);
/* ASCII representation for the small value column. */
void nabo_keys_chord(const nabo_key_packet_t *p, nabo_keys_os_t os, char *out, size_t cap);
