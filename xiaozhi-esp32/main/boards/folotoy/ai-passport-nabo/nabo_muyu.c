/* Offline wooden fish. UI publications never write Flash or touch I2S. */
#include "nabo_muyu.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#define SAVE_DEBOUNCE_US INT64_C(2000000)
static const char *TAG="nabo.muyu";
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static uint64_t s_count;
static uint32_t s_revision;
static bool s_wanted, s_pending, s_sound=true, s_dirty, s_initialized, s_sound_touched;
static int64_t s_last_change, s_last_attempt;

void nabo_muyu_open(void)
{
    portENTER_CRITICAL(&s_lock);
    s_wanted=true;
    portEXIT_CRITICAL(&s_lock);
}

void nabo_muyu_close(void)
{
    portENTER_CRITICAL(&s_lock);
    s_wanted=false; s_pending=false;
    portEXIT_CRITICAL(&s_lock);
}

bool nabo_muyu_wanted(void)
{
    portENTER_CRITICAL(&s_lock);
    bool wanted=s_wanted;
    portEXIT_CRITICAL(&s_lock);
    return wanted;
}

uint64_t nabo_muyu_count(void)
{
    portENTER_CRITICAL(&s_lock);
    uint64_t count=s_count;
    portEXIT_CRITICAL(&s_lock);
    return count;
}

bool nabo_muyu_hit(void)
{
    int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    bool accepted=s_wanted;
    if (accepted) {
        if (s_count<UINT64_MAX) ++s_count;
        ++s_revision; s_dirty=true; s_last_change=now;
        /* Rapid taps keep their count and visual response, without a backlog
         * of old knocks replaying after the user has stopped. */
        if (s_sound) s_pending=true;
    }
    portEXIT_CRITICAL(&s_lock);
    return accepted;
}

bool nabo_muyu_sound_enabled(void)
{
    portENTER_CRITICAL(&s_lock);
    bool enabled=s_sound;
    portEXIT_CRITICAL(&s_lock);
    return enabled;
}

void nabo_muyu_set_sound(bool enabled)
{
    int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    if (s_sound!=enabled || !s_initialized) {
        s_sound=enabled; s_sound_touched=true;
        ++s_revision; s_dirty=true; s_last_change=now;
    }
    if (!enabled) s_pending=false;
    portEXIT_CRITICAL(&s_lock);
}

bool nabo_muyu_take_hit(void)
{
    portENTER_CRITICAL(&s_lock);
    bool hit=s_wanted && s_sound && s_pending;
    s_pending=false;
    portEXIT_CRITICAL(&s_lock);
    return hit;
}

void nabo_muyu_init(void)
{
    portENTER_CRITICAL(&s_lock);
    bool initialized=s_initialized;
    portEXIT_CRITICAL(&s_lock);
    if (initialized) return;
    uint64_t count=0;
    uint8_t sound=1;
    nvs_handle_t h;
    if (nvs_open("nabo_muyu",NVS_READONLY,&h)==ESP_OK) {
        if (nvs_get_u64(h,"count",&count)!=ESP_OK) count=0;
        if (nvs_get_u8(h,"sound",&sound)!=ESP_OK || sound>1) sound=1;
        nvs_close(h);
    }
    portENTER_CRITICAL(&s_lock);
    /* Input starts before the voice task on this SDK. Merge any early taps
     * rather than replacing them when the saved snapshot finishes loading. */
    s_count=UINT64_MAX-count<s_count ? UINT64_MAX : count+s_count;
    if (!s_sound_touched) s_sound=sound!=0;
    if (!s_sound) s_pending=false;
    s_initialized=true;
    portEXIT_CRITICAL(&s_lock);
}

void nabo_muyu_save(void)
{
    int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    bool dirty=s_dirty && s_initialized;
    uint64_t count=s_count;
    bool sound=s_sound;
    uint32_t revision=s_revision;
    if (dirty) s_last_attempt=now;
    portEXIT_CRITICAL(&s_lock);
    if (!dirty) return;
    nvs_handle_t h;
    esp_err_t err=nvs_open("nabo_muyu",NVS_READWRITE,&h);
    if (err==ESP_OK) {
        err=nvs_set_u64(h,"count",count);
        if (err==ESP_OK) err=nvs_set_u8(h,"sound",sound ? 1 : 0);
        if (err==ESP_OK) err=nvs_commit(h);
        nvs_close(h);
    }
    portENTER_CRITICAL(&s_lock);
    if (err==ESP_OK && revision==s_revision) s_dirty=false;
    portEXIT_CRITICAL(&s_lock);
    if (err!=ESP_OK) ESP_LOGW(TAG,"Wooden-fish settings save failed: %d",err);
}

void nabo_muyu_idle_tick(void)
{
    int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    bool due=s_initialized && s_dirty && now-s_last_change>=SAVE_DEBOUNCE_US &&
             now-s_last_attempt>=SAVE_DEBOUNCE_US;
    portEXIT_CRITICAL(&s_lock);
    if (due) nabo_muyu_save();
}
