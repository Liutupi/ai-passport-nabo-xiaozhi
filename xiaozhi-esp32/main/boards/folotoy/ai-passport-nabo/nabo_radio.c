/* Tab5's station catalog on the no-PSRAM Passport; no Flash audio spooling. */
#include "nabo_radio.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "muse_settings.h"
#include "muse_wifi.h"
#include "nabo_radio_decoder.h"
#include "nabo_radio_stations.h"
#include "nabo_radio_stream.h"
#include "nvs.h"

#define INPUT_BYTES 4096
#define PCM_BYTES (12 * 1024)
#define PCM_PREFILL (8 * 1024)
#define RESAMPLE_CHUNK 160
static const char* TAG = "nabo.radio";
enum { STOPPED, CONNECTING, BUFFERING, PLAYING, WAIT_WIFI, RETRYING, FAILED, PAUSED };
static atomic_int s_state, s_station;
static atomic_uint s_generation, s_played, s_short_reads;
static atomic_bool s_wanted, s_active, s_done = ATOMIC_VAR_INIT(true), s_settings_dirty;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t s_favorites;
static StreamBufferHandle_t s_pcm;
static TaskHandle_t s_worker; /* Voice owner only; the producer never deletes itself. */
static bool s_prefilled;
static int64_t s_retry_at;
static unsigned s_seen_generation;
static bool s_inited;
static unsigned s_retry_delay = 2;

int nabo_radio_station_count(void) { return NABO_RADIO_STATION_COUNT; }
int nabo_radio_category_count(void) { return NABO_RADIO_CATEGORY_COUNT; }
const char* nabo_radio_station_name(int i) {
    return i >= 0 && i < NABO_RADIO_STATION_COUNT ? nabo_radio_stations[i].name : "";
}
int nabo_radio_station_category(int i) {
    return i >= 0 && i < NABO_RADIO_STATION_COUNT ? nabo_radio_stations[i].category : -1;
}
const char* nabo_radio_category_name(int i) {
    return i >= 0 && i < NABO_RADIO_CATEGORY_COUNT ? nabo_radio_categories[i] : "";
}
int nabo_radio_current_station(void) { return atomic_load(&s_station); }
bool nabo_radio_available(void) { return NABO_RADIO_STATION_COUNT > 0; }
bool nabo_radio_wanted(void) { return atomic_load(&s_wanted); }
bool nabo_radio_active(void) { return atomic_load(&s_active); }
const char* nabo_radio_state_text(void) {
    switch (atomic_load(&s_state)) {
        case CONNECTING:
            return "连接中";
        case BUFFERING:
            return "缓冲中";
        case PLAYING:
            return "播放中 MP3 64k";
        case WAIT_WIFI:
            return "等待 Wi-Fi";
        case RETRYING:
            return "断线，自动重连中";
        case FAILED:
            return "此电台暂不可用";
        case PAUSED:
            return "已暂停";
        default:
            return "已停止";
    }
}

bool nabo_radio_favorite(int i) {
    if (i < 0 || i >= NABO_RADIO_STATION_COUNT)
        return false;
    portENTER_CRITICAL(&s_lock);
    bool yes = (s_favorites & (UINT64_C(1) << i)) != 0;
    portEXIT_CRITICAL(&s_lock);
    return yes;
}
void nabo_radio_toggle_favorite(int i) {
    if (i < 0 || i >= NABO_RADIO_STATION_COUNT)
        return;
    portENTER_CRITICAL(&s_lock);
    s_favorites ^= UINT64_C(1) << i;
    portEXIT_CRITICAL(&s_lock);
    atomic_store(&s_settings_dirty, true);
}
static void save_settings(void) {
    if (!atomic_exchange(&s_settings_dirty, false))
        return;
    uint64_t favorites;
    portENTER_CRITICAL(&s_lock);
    favorites = s_favorites;
    portEXIT_CRITICAL(&s_lock);
    nvs_handle_t h;
    if (nvs_open("nabo_radio", NVS_READWRITE, &h) != ESP_OK) {
        atomic_store(&s_settings_dirty, true);
        return;
    }
    esp_err_t err = nvs_set_u64(h, "favorites", favorites);
    if (err == ESP_OK)
        err = nvs_set_u32(h, "station", (uint32_t)atomic_load(&s_station));
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        atomic_store(&s_settings_dirty, true);
        ESP_LOGW(TAG, "Radio settings save failed");
    }
}
void nabo_radio_init(void) {
    if (s_inited)
        return;
    ESP_LOGI(TAG,
             "Helix DNA Client technology included. Copyright (c) RealNetworks, Inc., "
             "1995-2002. All rights reserved. Covered source: third_party/helix-mp3 in project "
             "repository.");
    nvs_handle_t h;
    if (nvs_open("nabo_radio", NVS_READONLY, &h) == ESP_OK) {
        uint64_t favorites = 0;
        uint32_t station = 0;
        nvs_get_u64(h, "favorites", &favorites);
        nvs_get_u32(h, "station", &station);
        nvs_close(h);
        portENTER_CRITICAL(&s_lock);
        s_favorites = favorites;
        portEXIT_CRITICAL(&s_lock);
        if (station < NABO_RADIO_STATION_COUNT)
            atomic_store(&s_station, (int)station);
    }
    s_inited = true; /* Boot remembers the station, but never starts sound automatically. */
}
void nabo_radio_idle_tick(void) {
    if (!nabo_radio_active())
        save_settings();
}
void nabo_radio_select_station(int i) {
    if (i < 0 || i >= NABO_RADIO_STATION_COUNT)
        return;
    atomic_store(&s_station, i);
    atomic_store(&s_settings_dirty, true);
    atomic_fetch_add(&s_generation, 1);
    atomic_store(&s_wanted, true);
}
void nabo_radio_toggle_play(void) {
    if (atomic_load(&s_wanted)) {
        atomic_store(&s_wanted, false);
        atomic_store(&s_state, PAUSED);
    } else {
        atomic_fetch_add(&s_generation, 1);
        atomic_store(&s_wanted, true);
    }
}
void nabo_radio_stop(void) {
    atomic_store(&s_wanted, false);
    atomic_store(&s_state, STOPPED);
}
bool nabo_radio_due(void) {
    unsigned generation = atomic_load(&s_generation);
    if (generation != s_seen_generation) {
        s_seen_generation = generation;
        s_retry_at = 0;
        s_retry_delay = 2;
    }
    if (!atomic_load(&s_wanted) || atomic_load(&s_active))
        return false;
    if (!muse_wifi_connected()) {
        atomic_store(&s_state, WAIT_WIFI);
        return false;
    }
    if (!muse_settings_speaker_on()) {
        nabo_radio_stop();
        return false;
    }
    return esp_timer_get_time() >= s_retry_at;
}
typedef struct {
    nabo_icy_t icy;
    bool invalid;
} headers_t;
static esp_err_t on_http(esp_http_client_event_t* ev) {
    headers_t* h = ev->user_data;
    if (ev->event_id != HTTP_EVENT_ON_HEADER || !h)
        return ESP_OK;
    if (!strcasecmp(ev->header_key, "icy-metaint")) {
        char* end;
        errno = 0;
        unsigned long n = strtoul(ev->header_value, &end, 10);
        if (errno || *end || !n || n > 1048576)
            h->invalid = true;
        else
            nabo_icy_init(&h->icy, n);
    } else if (!strcasecmp(ev->header_key, "Content-Type")) {
        if (strstr(ev->header_value, "aac") || strstr(ev->header_value, "mpegurl") ||
            strstr(ev->header_value, "text/html"))
            h->invalid = true;
    }
    return ESP_OK;
}
static bool cancelled(unsigned generation) {
    return !atomic_load(&s_wanted) || atomic_load(&s_generation) != generation ||
           !muse_settings_speaker_on() || !muse_wifi_connected();
}
static bool send_pcm(const int16_t* pcm, size_t frames, unsigned generation) {
    const uint8_t* p = (const uint8_t*)pcm;
    size_t left = frames * 2;
    while (left && !cancelled(generation)) {
        size_t n = xStreamBufferSend(s_pcm, p, left, pdMS_TO_TICKS(20));
        p += n;
        left -= n;
    }
    return left == 0;
}
static void worker(void* arg) {
    (void)arg;
    unsigned generation = atomic_load(&s_generation);
    int station = atomic_load(&s_station), frames = 0, rate = 0;
    char stream_url[192];
    const char* catalog_url = nabo_radio_stations[station].url;
    // These catalog entries are public Qingting broadcasts. Their same-host
    // HTTP MP3 endpoints leave enough C3 RAM for Helix and the old PCM prefill.
    // Private XiaoZhi traffic continues through its own TLS transport.
    bool public_qingting =
        !strncmp(catalog_url, "https://", 8) &&
        (strstr(catalog_url, ".qtfm.cn/") || strstr(catalog_url, ".qingting.fm/"));
    snprintf(stream_url, sizeof(stream_url), public_qingting ? "http://%s" : "%s",
             public_qingting ? catalog_url + 8 : catalog_url);
    bool failure = false; /* The voice owner retains the network gate through finish(). */
    esp_http_client_handle_t http = NULL;
    nabo_radio_decoder_t* decoder = NULL;
    uint8_t* input = NULL;
    int16_t *pcm = NULL, *mono16 = NULL;
    size_t used = 0, received = 0, id3_left = 0;
    bool leading = true, need_more = false;
    headers_t headers = {0};
    nabo_radio_resampler_t resampler = {0};
    int64_t decode_us = 0, last_data = esp_timer_get_time(), next_progress = last_data + 5000000;
    size_t total_samples = 0;
    atomic_store(&s_state, CONNECTING);
    if (cancelled(generation))
        goto cleanup;
    save_settings();
    // Reserve the input block before TLS fragments the single-core C3 heap.
    input = malloc(INPUT_BYTES);
    if (!input) {
        failure = true;
        goto cleanup;
    }
    esp_http_client_config_t cfg = {
        .url = stream_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .event_handler = on_http,
        .user_data = &headers,
        .max_redirection_count = 3,
#if CONFIG_MBEDTLS_DYNAMIC_BUFFER
        /* Reserve the record buffer after TLS handshake and retain it while
         * the smaller MP3 allocations are made, as in the TTS client. */
        .tls_dyn_buf_strategy = HTTP_TLS_DYN_BUF_RX_STATIC,
#endif
    };
    http = esp_http_client_init(&cfg);
    if (!http) {
        failure = true;
        goto cleanup;
    }
    esp_http_client_set_header(http, "Icy-MetaData", "0");
    esp_http_client_set_header(http, "User-Agent", "NABO-Passport/1.0");
    esp_err_t open_result = esp_http_client_open(http, 0);
    if (open_result != ESP_OK) {
        ESP_LOGW(TAG, "Radio HTTP open failed: err=%ld heap=%u largest=%u", (long)open_result,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        failure = true;
        goto cleanup;
    }
    if (cancelled(generation))
        goto cleanup;
    int64_t header_result = esp_http_client_fetch_headers(http);
    int http_status = header_result < 0 ? -1 : esp_http_client_get_status_code(http);
    ESP_LOGI(TAG, "Radio HTTP headers: result=%lld status=%d invalid=%d heap=%u largest=%u",
             (long long)header_result, http_status, (int)headers.invalid,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (header_result < 0 || http_status != 200 || headers.invalid) {
        failure = true;
        goto cleanup;
    }
    if (cancelled(generation))
        goto cleanup;
    esp_http_client_set_timeout_ms(http, 1000);
    /* TLS needs a contiguous ~17KiB RX allocation. Complete that handshake
     * and header read before Helix and PCM fragments consume the same heap. */
    decoder = malloc(sizeof(*decoder));
    if (decoder)
        memset(decoder, 0, sizeof(*decoder));
    pcm = malloc(NABO_RADIO_MAX_SAMPLES_PER_FRAME * sizeof(int16_t));
    mono16 = malloc((RESAMPLE_CHUNK * 2 + 2) * sizeof(int16_t));
    ESP_LOGI(TAG, "Radio buffers: decoder=%d input=%d pcm=%d mono=%d heap=%u largest=%u",
             decoder != NULL, input != NULL, pcm != NULL, mono16 != NULL,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (!decoder || !input || !pcm || !mono16) {
        failure = true;
        goto cleanup;
    }
    if (!nabo_radio_decoder_init(decoder)) {
        ESP_LOGW(TAG, "Radio decoder init failed: heap=%u largest=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        failure = true;
        goto cleanup;
    }
    if (cancelled(generation))
        goto cleanup;
    last_data = esp_timer_get_time();
    atomic_store(&s_state, BUFFERING);
    ESP_LOGI(TAG, "Radio HTTP 200: station=%d, heap=%u largest=%u", station,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    while (!cancelled(generation)) {
        if (esp_timer_get_time() - last_data > 15000000) {
            failure = true;
            break;
        }
        if (used < 4096 || need_more) {
            if (INPUT_BYTES - used < 1024) {
                failure = true;
                break;
            }
            uint8_t raw[1024];
            int n = esp_http_client_read(http, (char*)raw, sizeof(raw));
            if (n == -ESP_ERR_HTTP_EAGAIN)
                continue;
            if (n <= 0) {
                failure = true;
                break;
            }
            size_t clean = 0;
            if (!nabo_icy_feed(&headers.icy, raw, (size_t)n, input + used, INPUT_BYTES - used,
                               &clean)) {
                failure = true;
                break;
            }
            used += clean;
            received += clean;
            need_more = false;
            if (clean)
                last_data = esp_timer_get_time();
        }
        if (leading) {
            if (used < 10)
                continue;
            if (!id3_left && !memcmp(input, "ID3", 3)) {
                if ((input[6] | input[7] | input[8] | input[9]) & 128) {
                    failure = true;
                    break;
                }
                id3_left = 10 + ((size_t)input[6] << 21) + ((size_t)input[7] << 14) +
                           ((size_t)input[8] << 7) + input[9];
                if (id3_left > 65536) {
                    failure = true;
                    break;
                }
            }
            if (id3_left) {
                size_t skip = used < id3_left ? used : id3_left;
                used -= skip;
                id3_left -= skip;
                memmove(input, input + skip, used);
                if (id3_left)
                    continue;
            }
            leading = false;
        }
        if (!frames && received > 96 * 1024) {
            failure = true;
            break;
        }
        while (used > 2048 && !cancelled(generation)) {
            nabo_radio_frame_info_t info = {0};
            int64_t start = esp_timer_get_time();
            int n = nabo_radio_decoder_decode(decoder, input, (int)used, pcm, &info);
            decode_us += esp_timer_get_time() - start;
            if (!info.frame_bytes) {
                need_more = true;
                break;
            }
            if (info.frame_bytes < 0 || (size_t)info.frame_bytes > used) {
                failure = true;
                goto cleanup;
            }
            used -= (size_t)info.frame_bytes;
            memmove(input, input + info.frame_bytes, used);
            if (!n)
                continue;
            if (n > 1152 || (info.channels != 1 && info.channels != 2) || info.hz < 8000 ||
                info.hz > 48000) {
                failure = true;
                goto cleanup;
            }
            if (rate != info.hz) {
                rate = info.hz;
                nabo_radio_resampler_init(&resampler, (unsigned)rate);
                ESP_LOGI(TAG, "MP3 %d Hz, %d channels, %d kbps", rate, info.channels,
                         info.bitrate_kbps);
            }
            if (info.channels == 2)
                for (int i = 0; i < n; ++i)
                    pcm[i] = (int16_t)(((int32_t)pcm[2 * i] + pcm[2 * i + 1]) / 2);
            // Resample bounded slices instead of another complete 4.6KiB frame.
            // The resampler retains phase and the previous sample across slices.
            for (size_t at = 0; at < (size_t)n; at += RESAMPLE_CHUNK) {
                size_t take = (size_t)n - at;
                if (take > RESAMPLE_CHUNK)
                    take = RESAMPLE_CHUNK;
                size_t out =
                    nabo_radio_resample(&resampler, pcm + at, take, mono16, RESAMPLE_CHUNK * 2 + 2);
                if (!out || !send_pcm(mono16, out, generation))
                    goto cleanup;
                total_samples += out;
            }
            ++frames;
            if (esp_timer_get_time() >= next_progress) {
                ESP_LOGI(TAG, "Radio progress: frames=%d played=%u short_reads=%u buffered=%u",
                         frames, atomic_load(&s_played), atomic_load(&s_short_reads),
                         (unsigned)xStreamBufferBytesAvailable(s_pcm));
                next_progress = esp_timer_get_time() + 5000000;
            }
        }
        if (used >= INPUT_BYTES - 1024) {
            failure = true;
            break;
        }
    }
cleanup:
    if (http)
        esp_http_client_cleanup(http);
    if (decoder)
        nabo_radio_decoder_free(decoder);
    free(decoder);
    free(input);
    free(pcm);
    free(mono16);
    /* Keep Muse reconnects blocked until the voice owner has also released
     * the PCM ring and allowed this task's stack to be reclaimed. */
    if (failure && !cancelled(generation))
        atomic_store(&s_state, FAILED);
    ESP_LOGI(TAG,
             "Radio end: frames=%d pcm=%u decode_ms=%u played=%u short_reads=%u failure=%d "
             "cancelled=%d stack_free=%u",
             frames, (unsigned)total_samples, (unsigned)(decode_us / 1000), atomic_load(&s_played),
             atomic_load(&s_short_reads), (int)failure, (int)cancelled(generation),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    atomic_store_explicit(&s_done, true, memory_order_release);
    /* On single-core C3 the voice owner may preempt here and delete this
     * non-current task before it reaches suspend. No resources are touched
     * after done; external deletion synchronously reclaims stack and TCB. */
    for (;;)
        vTaskSuspend(NULL);
}

bool nabo_radio_begin(void) {
    if (!nabo_radio_due())
        return false;
    /* Release Muse's TLS/buffers before a large internal stack/ring allocation. */
    if (!app_nabo_radio_network_begin()) {
        s_retry_at = esp_timer_get_time() + 500000;
        return false;
    }
    s_worker = NULL;
    s_pcm = xStreamBufferCreate(PCM_BYTES, 2);
    if (!s_pcm) {
        app_nabo_radio_network_end();
        atomic_store(&s_state, FAILED);
        s_retry_at = esp_timer_get_time() + 2000000;
        return false;
    }
    s_prefilled = false;
    atomic_store(&s_done, false);
    atomic_store(&s_played, 0);
    atomic_store(&s_short_reads, 0);
    atomic_store(&s_active, true);
    if (xTaskCreate(worker, "nabo_radio", 8 * 1024, NULL, 5, &s_worker) != pdPASS) {
        s_worker = NULL;
        vStreamBufferDelete(s_pcm);
        s_pcm = NULL;
        atomic_store(&s_active, false);
        atomic_store(&s_done, true);
        app_nabo_radio_network_end();
        atomic_store(&s_state, FAILED);
        s_retry_at = esp_timer_get_time() + 2000000;
        return false;
    }
    return true;
}
size_t nabo_radio_read(int16_t* pcm, size_t frames, int wait_ms) {
    if (!pcm || !frames || !s_pcm || !atomic_load(&s_wanted))
        return 0;
    if (!s_prefilled && !atomic_load(&s_done)) {
        if (xStreamBufferBytesAvailable(s_pcm) < PCM_PREFILL)
            return 0;
        s_prefilled = true;
        atomic_store(&s_state, PLAYING);
    }
    size_t n =
        xStreamBufferReceive(s_pcm, pcm, frames * 2, pdMS_TO_TICKS(wait_ms > 0 ? wait_ms : 0));
    if (n < frames * 2 && !atomic_load(&s_done))
        atomic_fetch_add(&s_short_reads, 1);
    atomic_fetch_add(&s_played, (unsigned)(n / 2));
    return n / 2;
}
bool nabo_radio_finished(void) {
    return atomic_load_explicit(&s_done, memory_order_acquire) &&
           (!s_pcm || !atomic_load(&s_wanted) || !xStreamBufferBytesAvailable(s_pcm));
}
void nabo_radio_finish(void) {
    if (!atomic_load_explicit(&s_done, memory_order_acquire) || !atomic_load(&s_active))
        return;
    /* C3 is single-core: the producer is non-current even if this task
     * preempted it between publishing done and suspending. Free its stack/TCB
     * synchronously before allowing Muse to allocate a new TLS session. */
    if (s_worker) {
        TaskHandle_t worker = s_worker;
        s_worker = NULL;
        vTaskDelete(worker);
    }
    if (s_pcm) {
        vStreamBufferDelete(s_pcm);
        s_pcm = NULL;
    }
    save_settings();
    app_nabo_radio_network_end();
    atomic_store(&s_active, false);
    if (atomic_load(&s_wanted)) {
        atomic_store(&s_state, RETRYING);
        s_retry_at = esp_timer_get_time() + (int64_t)s_retry_delay * 1000000;
        if (s_retry_delay < 30)
            s_retry_delay = s_retry_delay * 2 > 30 ? 30 : s_retry_delay * 2;
    }
}
