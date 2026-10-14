#pragma once
#include "nabo_keys_model.h"
#include <stdbool.h>

struct ble_gap_event;
/* UI calls only atomic/queue operations, never BLE teardown or NVS. */
void nabo_keys_open(void);
void nabo_keys_close(void);
void nabo_keys_apply(void);
bool nabo_keys_wanted(void);
nabo_keys_os_t nabo_keys_os(void);
void nabo_keys_toggle_os(void);
bool nabo_keys_send(nabo_key_t key);
bool nabo_keys_reconnect(void);
void nabo_keys_resolve(nabo_key_t key, nabo_key_packet_t *out);
void nabo_keys_status(char *out, size_t cap);
/* Called by the BLE owner/keeper, never the LVGL thread. */
void nabo_keys_prepare(void);
void nabo_keys_start(void);
bool nabo_keys_stop(void);
void nabo_keys_register(void);
void nabo_keys_gap(struct ble_gap_event *event);
/* USB-only configuration, validates and persists both platform maps. */
bool nabo_keys_command(char *line);
