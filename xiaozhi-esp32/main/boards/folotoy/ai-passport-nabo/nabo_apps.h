#pragma once
#include "lvgl.h"
#include "muse_menu.h"
enum NaboApp {
    NABO_HOME,
    NABO_TOOLS,
    NABO_SETTINGS,
    NABO_DICE,
    NABO_MUYU,
    NABO_RADIO,
    NABO_KEYS,
    NABO_JUMP,
    NABO_STATUS,
    NABO_BATTERY,
    NABO_WIFI
};
void nabo_apps_build(lv_obj_t* screen);
void nabo_apps_tick();
bool nabo_apps_key(muse_menu_key_t key);
void nabo_apps_charge(bool pressed, uint32_t event_ms = 0);
void nabo_apps_open(NaboApp app, int delay_ms = 0);
bool nabo_apps_active();
bool nabo_apps_is_jump();
void nabo_apps_register_tools();
void nabo_apps_initialize();
bool nabo_apps_voice_command(const char* transcript);

void nabo_apps_record_frame();
