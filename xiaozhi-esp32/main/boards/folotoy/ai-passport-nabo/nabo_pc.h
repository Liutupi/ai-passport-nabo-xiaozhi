#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint32_t passkey; } nabo_pc_status_t;
void nabo_pc_request_apply(void);
void nabo_pc_apply(void);
void nabo_pc_status(nabo_pc_status_t *out);
bool nabo_pc_running(void);
int nabo_pc_battery_level(void);
