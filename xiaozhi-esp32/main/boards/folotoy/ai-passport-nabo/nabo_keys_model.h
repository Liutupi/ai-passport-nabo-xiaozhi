#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { NABO_KEYS_MAC, NABO_KEYS_WINDOWS, NABO_KEYS_OS_COUNT } nabo_keys_os_t;
typedef enum {
    NABO_KEY_DICTATE, NABO_KEY_TRANSLATE, NABO_KEY_ASK,
    NABO_KEY_ENTER, NABO_KEY_NEWLINE, NABO_KEY_ESCAPE, NABO_KEY_TAB, NABO_KEY_UP,
    NABO_KEY_COPY, NABO_KEY_PASTE, NABO_KEY_CUT, NABO_KEY_UNDO, NABO_KEY_REDO,
    NABO_KEY_ALL, NABO_KEY_SAVE, NABO_KEY_FIND,
    NABO_KEY_SWITCH, NABO_KEY_OVERVIEW, NABO_KEY_SEARCH, NABO_KEY_LOCK,
    NABO_KEY_NEW_TAB, NABO_KEY_CLOSE_TAB, NABO_KEY_REOPEN_TAB, NABO_KEY_NEXT_TAB,
    NABO_KEY_PREV_TAB, NABO_KEY_ADDRESS,
    NABO_KEY_SCREEN_AREA, NABO_KEY_SCREEN_FULL,
    NABO_KEY_PLAY, NABO_KEY_PREVIOUS, NABO_KEY_NEXT, NABO_KEY_VOL_UP,
    NABO_KEY_VOL_DOWN, NABO_KEY_MUTE, NABO_KEY_COUNT
} nabo_key_t;
/* Report 1 is the standard eight-byte keyboard report; report 2 is a
 * little-endian 16-bit Consumer usage. Neither includes the report ID. */
typedef struct { uint8_t report, len, data[8]; } nabo_key_packet_t;
enum { NABO_KEYS_GROUPS = 5, NABO_KEYS_CONNECTION = 5 };
bool nabo_keys_packet(nabo_keys_os_t os, nabo_key_t key, nabo_key_packet_t *out);
bool nabo_keys_override_packet(uint16_t encoded, nabo_key_packet_t *out);
const char *nabo_key_name(nabo_key_t key);
const char *nabo_keys_group_name(int group);
int nabo_keys_group_count(int group);
nabo_key_t nabo_keys_group_item(int group, int row);
/* ASCII representation for the small value column. */
void nabo_keys_chord(const nabo_key_packet_t *p, nabo_keys_os_t os, char *out, size_t cap);
