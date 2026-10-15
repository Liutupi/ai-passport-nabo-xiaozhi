#include "nabo_apps.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "application.h"
#include "backlight.h"
#include "board.h"
#include "codecs/es8311_audio_codec.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "mcp_server.h"
#include "nabo_display.h"
#include "nvs.h"
#include "settings.h"
#include "src/draw/lv_image_decoder_private.h"
#include "ssid_manager.h"
#include "wifi_manager.h"
extern "C" {
#include "nabo_dice.h"
#include "nabo_dice_render.h"
#include "nabo_dice_ui.h"
#include "nabo_jump_model.h"
#include "nabo_jump_sound.h"
#include "nabo_keys.h"
#include "nabo_muyu.h"
#include "nabo_muyu_sound.h"
#include "nabo_muyu_ui.h"
#include "nabo_pc.h"
#include "nabo_radio.h"
#include "nabo_radio_menu.h"
#include "nabo_rhythm_play.h"
#include "nabo_voice_commands.h"
LV_FONT_DECLARE(nabo_menu_font_16);
LV_FONT_DECLARE(nabo_mono_font_12);
}
#include "nabo_rhythm_ui.h"

static std::atomic<int> wanted{NABO_HOME}, request_revision{0}, setting_command{-1};
static std::atomic<int> detail_parent{NABO_SETTINGS};
static std::atomic<int64_t> open_after{0}, last_input{0};
static std::atomic<bool> ready{false}, screen_off{false}, speaker{true}, wifi_on{true};
static std::atomic<int> volume{70}, brightness{80}, sleep_seconds{120}, mic_gain{30}, battery{-1};
static std::atomic<uint32_t> best_score{0};
static std::atomic<bool> best_dirty{false};
static std::atomic<int64_t> best_changed_at{0};
static std::atomic<int> jump_sound{0};
static lv_obj_t *app_screen, *panel, *title, *body, *footer, *rows[7], *values[7], *selection;
static lv_obj_t *tag, *icons[7], *scroll_track, *scroll_thumb, *card, *card_title, *card_sub,
    *meter, *meter_fill, *footer_rule;
// Menu palette: NABO black with the passport-green accent.
static constexpr uint32_t kMenuBg = 0x0b0e14, kMenuCard = 0x151b25, kMenuSelect = 0x1b2633,
                          kMenuEdit = 0x183a2c, kMenuAccent = 0x3ddc97, kMenuText = 0xe9edf3,
                          kMenuMuted = 0x7d8799, kMenuRule = 0x222a36;
static constexpr int kRowTop = 52, kRowPitch = 30;
static NaboApp page = NABO_HOME;
static int selected = 1, settings_selected = 1, edit = -1, key_group = -1, key_selected = 0,
           shown_revision = -1;
static bool initialized = false, engaged = false, restored = true, pc_owned = false;
static int64_t last_battery = 0;
static char notice[96] = "";
static nabo_jump_t jump_game, jump_frame;
static uint32_t jump_frame_time;
static std::atomic<uint32_t> jump_frames{0}, jump_fps{0};
struct JumpTiming {
    uint32_t frames = 0, intervals = 0, max_us = 0, slow = 0;
    uint64_t sum_us = 0;
    uint64_t draw_us = 0;
    uint32_t max_draw_us = 0;
};
static JumpTiming jump_timing[6];
static uint32_t jump_draw_us = 0;
static int64_t jump_previous_flush = 0;
static jump_phase_t jump_previous_phase = JUMP_READY;
static bool jump_test_mode = false;
static std::atomic<bool> jump_round_over{false};
void nabo_apps_record_frame() {
    if (nabo_apps_is_jump()) {
        jump_frames.fetch_add(1);
        auto phase = jump_frame.phase;
        auto& timing = jump_timing[phase];
        ++timing.frames;
        timing.draw_us += jump_draw_us;
        timing.max_draw_us = std::max(timing.max_draw_us, jump_draw_us);
        jump_draw_us = 0;
        int64_t now = esp_timer_get_time();
        if (jump_previous_flush && phase == jump_previous_phase) {
            uint32_t elapsed = now - jump_previous_flush;
            ++timing.intervals;
            timing.sum_us += elapsed;
            timing.max_us = std::max(timing.max_us, elapsed);
            if (elapsed > 50000)
                ++timing.slow;
        }
        jump_previous_flush = now;
        jump_previous_phase = phase;
        // At 100 Hz the LVGL port otherwise sleeps twice for at least one tick
        // after a long scroll redraw. Keep its display event pending so the
        // next due frame can run after the normal yield, without that extra wait.
        if (phase == JUMP_SCROLLING)
            lvgl_port_task_wake(LVGL_PORT_EVENT_USER, nullptr);
    }
}
static lv_obj_t *jump_panel, *jump_image, *jump_score, *jump_best, *jump_station, *jump_hint;
static lv_image_dsc_t jump_source = {};
static lv_draw_buf_t* jump_strip;
static int64_t jump_last_frame;

static const char* tools[] = {"返回 NABO", "跳一跳",   "节奏回声",   "摇骰子",
                              "功德木鱼",  "网络电台", "电脑快捷键", "更多设置"};
static const char* tool_icons[] = {LV_SYMBOL_HOME,       LV_SYMBOL_PLAY, LV_SYMBOL_AUDIO,
                                   LV_SYMBOL_SHUFFLE,    LV_SYMBOL_BELL, LV_SYMBOL_VOLUME_MAX,
                                   LV_SYMBOL_KEYBOARD,   LV_SYMBOL_SETTINGS};
static constexpr int kToolCount = sizeof(tools) / sizeof(*tools);
static const NaboApp tool_apps[] = {NABO_HOME, NABO_JUMP,  NABO_RHYTHM, NABO_DICE,
                                    NABO_MUYU, NABO_RADIO, NABO_KEYS,   NABO_SETTINGS};
enum Setting {
    ST_BACK,
    ST_VOLUME,
    ST_SPEAKER,
    ST_BRIGHTNESS,
    ST_SLEEP,
    ST_MIC,
    ST_WIFI_ADD,
    ST_WIFI,
    ST_STATE,
    ST_BATTERY,
    ST_SCREEN,
    ST_POWER,
    ST_COUNT
};
static const char* settings_names[] = {"返回",     "音量",       "扬声器",   "屏幕亮度",
                                       "自动休眠", "麦克风增益", "添加网络", "无线网络",
                                       "设备状态", "电池信息",   "关闭屏幕", "关机"};
static const char* settings_icons[] = {
    LV_SYMBOL_LEFT, LV_SYMBOL_VOLUME_MAX, LV_SYMBOL_AUDIO, LV_SYMBOL_EYE_OPEN,
    LV_SYMBOL_PAUSE, LV_SYMBOL_CALL, LV_SYMBOL_PLUS, LV_SYMBOL_WIFI,
    LV_SYMBOL_LIST, LV_SYMBOL_BATTERY_3, LV_SYMBOL_EYE_CLOSE, LV_SYMBOL_POWER};

extern "C" bool muse_settings_speaker_on(void) { return speaker.load(); }
extern "C" bool muse_wifi_connected(void) {
    return wifi_on.load() && WifiManager::GetInstance().IsConnected();
}
extern "C" int nabo_pc_battery_level(void) { return battery.load(); }
extern "C" bool app_nabo_radio_network_begin(void) {
    return ready.load() && Application::GetInstance().IsLocalAppActive();
}
// XiaoZhi stays paused while its local program is open. The worker resumes it
// only after every decoder and BLE host has stopped on return to NABO.
extern "C" void app_nabo_radio_network_end(void) {}
extern "C" bool app_nabo_radio_active(void) { return nabo_radio_active(); }

void nabo_apps_open(NaboApp app, int delay_ms) {
    if (app < NABO_HOME || app > NABO_RHYTHM)
        return;
    int origin = wanted.load();
    if ((app == NABO_STATUS || app == NABO_BATTERY || app == NABO_WIFI) && origin != app)
        detail_parent = origin == NABO_HOME       ? NABO_HOME
                        : origin == NABO_SETTINGS ? NABO_SETTINGS
                                                  : NABO_TOOLS;
    open_after.store(esp_timer_get_time() + static_cast<int64_t>(delay_ms) * 1000);
    wanted.store(app);
    request_revision.fetch_add(1);
    last_input.store(esp_timer_get_time());
}
bool nabo_apps_active() { return wanted.load() != NABO_HOME || ready.load(); }
bool nabo_apps_is_jump() { return wanted.load() == NABO_JUMP && ready.load(); }
bool nabo_apps_is_fast() {
    return (wanted.load() == NABO_JUMP || wanted.load() == NABO_RHYTHM) && ready.load();
}

static void save_settings() {
    nvs_handle_t h;
    esp_err_t err = nvs_open("nabo_ui", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, "volume", volume.load());
        if (!err)
            err = nvs_set_i32(h, "brightness", brightness.load());
        if (!err)
            err = nvs_set_i32(h, "sleep", sleep_seconds.load());
        if (!err)
            err = nvs_set_i32(h, "mic", mic_gain.load());
        if (!err)
            err = nvs_set_u8(h, "speaker", speaker.load());
        if (!err)
            err = nvs_set_u8(h, "wifi", wifi_on.load());
        if (!err)
            err = nvs_commit(h);
        nvs_close(h);
    }
    if (err) {
        snprintf(notice, sizeof(notice), "设置未保存，请重试");
        ESP_LOGE("NABO-Apps", "settings save: %d", err);
    }
}
static void load_settings() {
    auto& board = Board::GetInstance();
    volume = board.GetAudioCodec()->output_volume();
    brightness = board.GetBacklight()->brightness();
    nvs_handle_t h;
    if (nvs_open("nabo_ui", NVS_READONLY, &h) == ESP_OK) {
        int32_t v;
        uint8_t flag;
        if (!nvs_get_i32(h, "volume", &v))
            volume = std::clamp<int>(v, 0, 100);
        if (!nvs_get_i32(h, "brightness", &v))
            brightness = std::clamp<int>(v, 10, 100);
        if (!nvs_get_i32(h, "sleep", &v))
            sleep_seconds = std::clamp<int>(v, 0, 600);
        if (!nvs_get_i32(h, "mic", &v))
            mic_gain = std::clamp<int>(v, 0, 36);
        if (!nvs_get_u8(h, "speaker", &flag))
            speaker = flag != 0;
        if (!nvs_get_u8(h, "wifi", &flag))
            wifi_on = flag != 0;
        uint32_t score;
        if (!nvs_get_u32(h, "jump_best", &score))
            best_score = score;
        nvs_close(h);
    }
    board.GetAudioCodec()->SetOutputVolume(speaker ? volume.load() : 0);
    board.GetAudioCodec()->SetInputGain(mic_gain.load());
    board.GetBacklight()->SetBrightness(brightness.load(), false);
}
static void output16(const int16_t* input, size_t count) {
    if (!speaker.load() || !ready.load() || !count)
        return;
    auto codec = Board::GetInstance().GetAudioCodec();
    static std::vector<int16_t> pcm;
    pcm.assign(input, input + count);
    codec->EnableOutput(true);
    codec->OutputData(pcm);
}
static void apply_setting(int id) {
    auto& board = Board::GetInstance();
    switch (id) {
        case ST_VOLUME:
            board.GetAudioCodec()->SetOutputVolume(speaker ? volume.load() : 0);
            break;
        case ST_SPEAKER:
            if (!speaker)
                nabo_radio_stop();
            board.GetAudioCodec()->SetOutputVolume(speaker ? volume.load() : 0);
            break;
        case ST_BRIGHTNESS:
            board.GetBacklight()->SetBrightness(brightness.load(), true);
            break;
        case ST_MIC:
            board.GetAudioCodec()->SetInputGain(mic_gain.load());
            break;
        case ST_WIFI:
            if (wifi_on)
                WifiManager::GetInstance().StartStation();
            else {
                nabo_radio_stop();
                WifiManager::GetInstance().StopStation();
            }
            break;
        case ST_WIFI_ADD:
            WifiManager::GetInstance().StartConfigAp();
            break;
        case ST_SCREEN:
            screen_off = true;
            board.GetBacklight()->SetBrightness(0, false);
            break;
        case ST_POWER:
            nabo_muyu_save();
            save_settings();
            board.GetBacklight()->SetBrightness(0, false);
            board.GetAudioCodec()->EnableInput(false);
            board.GetAudioCodec()->EnableOutput(false);
            esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << 0,
                                                                ESP_GPIO_WAKEUP_GPIO_LOW);
            esp_deep_sleep_start();
            break;
        default:
            break;
    }
    save_settings();
}
static void diagnostic(const char* line) {
    auto& app = Application::GetInstance();
    if (!strcmp(line, ">apps.status")) {
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        printf(
            "\n@apps "
            "{\"wanted\":%d,\"page\":%d,\"ready\":%d,\"local\":%d,\"heap\":%u,\"radio\":%d,\"pc\":%"
            "d,\"fps\":%lu,\"volume\":%d,\"brightness\":%d,\"sleep\":%d,\"score\":%lu,\"best\":%lu,"
            "\"wifi\":%d,\"phase\":%d,\"gap\":%d,\"queued\":%d,\"holding\":%d,\"held_ms\":%lu,"
            "\"distance\":%d,\"edit\":%d,\"group\":%d,\"cursor\":%d,\"detail_parent\":%d}"
            "\n",
            wanted.load(), (int)page, ready.load(), app.IsLocalAppActive(),
            (unsigned)esp_get_free_heap_size(), nabo_radio_active(), nabo_pc_running(),
            (unsigned long)jump_fps.load(), volume.load(), brightness.load(), sleep_seconds.load(),
            (unsigned long)jump_game.score, (unsigned long)best_score.load(),
            WifiManager::GetInstance().IsConnected(), (int)jump_game.phase, jump_game.gap,
            jump_game.pending_charge, nabo_jump_charging(&jump_game),
            (unsigned long)nabo_jump_charge_ms(&jump_game, esp_timer_get_time() / 1000),
            jump_game.distance, edit, key_group, key_selected, detail_parent.load());
        if (page == NABO_RADIO) {
            char text[80];
            nabo_radio_menu_text(text, sizeof(text));
            if (char* newline = strchr(text, '\n'))
                *newline = 0;
            printf("@radio-menu %s\n", text);
        }
    } else if (!strcmp(line, ">jump.metrics")) {
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        for (int phase = 0; phase < 6; ++phase) {
            const auto& timing = jump_timing[phase];
            printf(
                "\n@jump-timing "
                "{\"phase\":%d,\"frames\":%lu,\"intervals\":%lu,\"mean_us\":%lu,\"max_us\":%lu,"
                "\"over_50ms\":%lu,\"mean_draw_us\":%lu,\"max_draw_us\":%lu}\n",
                phase, (unsigned long)timing.frames, (unsigned long)timing.intervals,
                (unsigned long)(timing.intervals ? timing.sum_us / timing.intervals : 0),
                (unsigned long)timing.max_us, (unsigned long)timing.slow,
                (unsigned long)(timing.frames ? timing.draw_us / timing.frames : 0),
                (unsigned long)timing.max_draw_us);
        }
    } else if (!strcmp(line, ">jump.metrics.reset")) {
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        for (auto& timing : jump_timing)
            timing = {};
        jump_previous_flush = 0;
    } else if (!strcmp(line, ">jump.test=1") || !strcmp(line, ">jump.test=0")) {
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        jump_test_mode = line[11] == '1';
        jump_game.best = best_score.load();
    } else if (!strncmp(line, ">jump.best=", 11)) {
        char* end;
        unsigned long restored_best = strtoul(line + 11, &end, 10);
        if (*end || restored_best > 1000000)
            return;
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        best_score = restored_best;
        jump_game.best = restored_best;
        best_dirty = true;
        best_changed_at = esp_timer_get_time();
    } else if (!strncmp(line, ">apps.open=", 11)) {
        const char* name = line + 11;
        struct {
            const char* name;
            NaboApp app;
        } aliases[] = {{"home", NABO_HOME},     {"tools", NABO_TOOLS}, {"settings", NABO_SETTINGS},
                       {"dice", NABO_DICE},     {"muyu", NABO_MUYU},   {"radio", NABO_RADIO},
                       {"computer", NABO_KEYS}, {"jump", NABO_JUMP},   {"status", NABO_STATUS},
                       {"rhythm", NABO_RHYTHM}};
        bool found = false;
        for (const auto& item : aliases)
            if (!strcmp(name, item.name)) {
                nabo_apps_open(item.app);
                found = true;
                break;
            }
        printf("\n@open %s\n", found ? "queued" : "invalid");
    } else if (!strncmp(line, ">apps.voice=", 12)) {
        const char* name = line + 12;
        if (strlen(name) > 20 || strspn(name, "abcdefghijklmnopqrstuvwxyz") != strlen(name))
            return;
        std::string message =
            "{\"jsonrpc\":\"2.0\",\"id\":919,\"method\":\"tools/"
            "call\",\"params\":{\"name\":\"self.apps.open\",\"arguments\":{\"app\":\"" +
            std::string(name) + "\"}}}";
        app.Schedule([message]() {
            McpServer::GetInstance().ParseMessage(message, [](const std::string& response) {
                printf("\n@mcp %s\n", response.c_str());
            });
        });
    } else if (!strncmp(line, ">apps.key=", 10)) {
        muse_menu_key_t key = MUSE_MENU_SELECT;
        if (!strcmp(line + 10, "up"))
            key = MUSE_MENU_UP;
        else if (!strcmp(line + 10, "down"))
            key = MUSE_MENU_DOWN;
        else if (!strcmp(line + 10, "back"))
            key = MUSE_MENU_BACK;
        else if (!strcmp(line + 10, "parent"))
            key = MUSE_MENU_LEFT;
        else if (strcmp(line + 10, "ok"))
            return;
        app.Schedule([key]() {
            auto display = Board::GetInstance().GetDisplay();
            DisplayLockGuard lock(display);
            nabo_apps_key(key);
        });
    } else if (!strncmp(line, ">jump.hold=", 11)) {
        char* end;
        long hold = strtol(line + 11, &end, 10);
        if (*end || hold < 0 || hold > 1400)
            return;
        app.Schedule([hold]() {
            auto display = Board::GetInstance().GetDisplay();
            DisplayLockGuard lock(display);
            if (nabo_apps_is_jump()) {
                uint32_t now = esp_timer_get_time() / 1000;
                nabo_jump_press(&jump_game, now - hold);
                nabo_jump_release(&jump_game, now);
                jump_sound = 1;
            }
        });
    } else if (!strcmp(line, ">rhythm.best.reset")) {
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        nabo_rhythm_ui_reset_best();
        printf("\n@rhythm best cleared\n");
    } else if (!strncmp(line, ">rhythm.latency=", 16)) {
        nabo_rhythm_play_set_latency_ms(atoi(line + 16));
        printf("\n@rhythm latency_ms=%d\n", nabo_rhythm_play_latency_ms());
    } else if (!strcmp(line, ">apps.capture")) {
        app.Schedule(
            []() { static_cast<NaboDisplay*>(Board::GetInstance().GetDisplay())->CaptureLCD(); });
    } else if (!strcmp(line, ">jump.press") || !strcmp(line, ">jump.release")) {
        bool pressed = !strcmp(line, ">jump.press");
        app.Schedule([pressed]() {
            DisplayLockGuard lock(Board::GetInstance().GetDisplay());
            nabo_apps_charge(pressed);
        });
    }
}
static void worker(void*) {
    vTaskDelay(pdMS_TO_TICKS(350));
    load_settings();
    nabo_muyu_init();
    nabo_radio_init();
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t cfg = {.tx_buffer_size = 2048, .rx_buffer_size = 512};
        usb_serial_jtag_driver_install(&cfg);
    }
    usb_serial_jtag_vfs_use_driver();
    char command_line[96] = {};
    unsigned command_len = 0;
    int16_t samples[320];
    size_t muyu_sample = NABO_MUYU_SOUND_FRAMES + 960;
    nabo_dice_sound_t dice_sound = {};
    bool dice_playing = false, usb_attached = false;
    nabo_jump_sound_t jump_audio = {};
    int64_t measured_at = esp_timer_get_time();
    uint32_t measured_frames = 0;
    for (;;) {
        char data[64];
        int received = usb_serial_jtag_read_bytes(data, sizeof(data), 0);
        for (int i = 0; i < received; ++i) {
            char c = data[i];
            if (c == '\n' || c == '\r') {
                command_line[command_len] = 0;
                if (command_len)
                    diagnostic(command_line);
                command_len = 0;
            } else if (command_len < sizeof(command_line) - 1)
                command_line[command_len++] = c;
            else
                command_len = 0;
        }
        auto& app = Application::GetInstance();
        int desired = wanted.load();
        int64_t now = esp_timer_get_time();
        if (desired != NABO_HOME && now >= open_after.load() && !engaged &&
            !app.IsLocalAppActive() && !app.IsProtocolInitializing() &&
            app.GetDeviceState() != kDeviceStateActivating &&
            app.GetDeviceState() != kDeviceStateStarting) {
            engaged = true;
            restored = false;
            app.BeginLocalApp();
        }
        if (engaged && !ready.load() && app.IsLocalAppActive()) {
            if (app.GetAudioService().WaitStopped(2000)) {
                vTaskDelay(pdMS_TO_TICKS(30));
                auto codec = static_cast<Es8311AudioCodec*>(Board::GetInstance().GetAudioCodec());
                codec->EnableInput(false);
                codec->EnableOutput(false);
                if (!codec->SetDuplexSampleRate(16000)) {
                    nabo_apps_open(NABO_HOME);
                    continue;
                }
                ready = true;
                ESP_LOGI("NABO-Apps", "local audio ownership ready, heap=%u",
                         (unsigned)esp_get_free_heap_size());
            } else {
                ESP_LOGE("NABO-Apps", "audio stop timeout; local playback withheld");
                nabo_apps_open(NABO_HOME);
            }
        }
        bool audio_clocked = false;
        if (ready.load()) {
            if (desired != NABO_JUMP)
                static_cast<NaboDisplay*>(Board::GetInstance().GetDisplay())
                    ->ReleaseGameDrawBuffer();
            if (desired == NABO_KEYS && !pc_owned && !nabo_radio_active()) {
                pc_owned = true;
                nabo_keys_open();
            }
            if (desired != NABO_KEYS && pc_owned) {
                pc_owned = false;
                nabo_keys_close();
            }
            nabo_pc_apply();
            if (desired != NABO_RADIO && nabo_radio_wanted())
                nabo_radio_stop();
            if (desired == NABO_RADIO && !nabo_pc_running() && nabo_radio_due())
                nabo_radio_begin();
            memset(samples, 0, sizeof(samples));
            if (nabo_radio_active()) {
                if (nabo_radio_finished())
                    nabo_radio_finish();
                else {
                    int16_t radio_pcm[320];
                    size_t n = nabo_radio_read(radio_pcm, 320, 0);
                    if (n && desired == NABO_RADIO)
                        memcpy(samples, radio_pcm, n * sizeof(int16_t));
                }
            }
            uint32_t seed;
            unsigned count;
            int64_t thrown_at;
            if (nabo_dice_take_throw(&seed, &count, &thrown_at)) {
                nabo_dice_plan_t plan;
                nabo_dice_plan(&plan, seed, count);
                uint32_t start = static_cast<uint32_t>(
                    std::max<int64_t>(0, esp_timer_get_time() - thrown_at + 40000) * 16000 /
                    1000000);
                nabo_dice_sound_init(&dice_sound, &plan, start);
                dice_playing = true;
            }
            if (desired == NABO_DICE && dice_playing) {
                if (!nabo_dice_sound_fill(&dice_sound, samples, 320))
                    dice_playing = false;
            } else
                dice_playing = false;
            // Complete each old-project knock and its DMA tail before taking
            // another pending hit; rapid taps must not chop the waveform.
            if (desired == NABO_MUYU && muyu_sample >= NABO_MUYU_SOUND_FRAMES + 960 &&
                nabo_muyu_take_hit())
                muyu_sample = 0;
            if (desired == NABO_MUYU && nabo_muyu_sound_enabled() &&
                muyu_sample < NABO_MUYU_SOUND_FRAMES) {
                size_t n = std::min<size_t>(320, NABO_MUYU_SOUND_FRAMES - muyu_sample);
                memcpy(samples, nabo_muyu_sound_pcm + muyu_sample, n * sizeof(int16_t));
                muyu_sample += n;
            } else if (desired == NABO_MUYU && muyu_sample < NABO_MUYU_SOUND_FRAMES + 960) {
                muyu_sample += 320;
            } else if (desired != NABO_MUYU)
                muyu_sample = NABO_MUYU_SOUND_FRAMES + 960;
            int tone = jump_sound.exchange(0);
            if (tone)
                nabo_jump_sound_start(&jump_audio, tone, 1);
            if (desired == NABO_JUMP)
                nabo_jump_sound_fill(&jump_audio, samples, 320);
            else
                nabo_jump_sound_start(&jump_audio, 0, 0);
            if (desired == NABO_RHYTHM)
                nabo_rhythm_audio_fill(samples, 320);
            // A continuous 20 ms clock prevents underruns and clears each effect's
            // DMA tail. Do not sleep again after the blocking I2S write.
            if (desired != NABO_HOME && speaker.load()) {
                output16(samples, 320);
                audio_clocked = true;
            }
            if (desired == NABO_HOME && !nabo_radio_active() && !nabo_pc_running()) {
                Board::GetInstance().GetAudioCodec()->EnableOutput(false);
                ready = false;
                engaged = false;
                if (!restored) {
                    restored = true;
                    app.EndLocalApp();
                }
            }
        }
        int command = setting_command.exchange(-1);
        if (command >= 0)
            apply_setting(command);
        nabo_muyu_idle_tick();
        nabo_radio_idle_tick();
        if (best_dirty.load() &&
            (desired != NABO_JUMP ||
             (jump_round_over.load() && now - best_changed_at.load() >= 2000000)) &&
            best_dirty.exchange(false)) {
            nvs_handle_t h;
            if (!nvs_open("nabo_ui", NVS_READWRITE, &h)) {
                nvs_set_u32(h, "jump_best", best_score.load());
                nvs_commit(h);
                nvs_close(h);
            }
        }
        if (now - last_battery > 3000000) {
            int pct;
            bool charging, discharging;
            if (Board::GetInstance().GetBatteryLevel(pct, charging, discharging))
                battery = pct;
            last_battery = now;
        }
        if (now - measured_at >= 5000000) {
            uint32_t frames = jump_frames.load();
            jump_fps = (frames - measured_frames) * 1000000ULL / (now - measured_at);
            if (desired == NABO_JUMP)
                ESP_LOGI("NABO-Jump", "display refresh fps=%lu", (unsigned long)jump_fps.load());
            else if (desired == NABO_HOME && !app.IsLocalAppActive() &&
                     !app.IsProtocolInitializing())
                app.GetAudioService().LogMemoryStats();
            measured_frames = frames;
            measured_at = now;
        }
        bool usb = usb_serial_jtag_is_connected();
        if (usb != usb_attached) {
            usb_attached = usb;
            last_input.store(now);
            if (usb && screen_off.exchange(false))
                Board::GetInstance().GetBacklight()->SetBrightness(brightness.load(), false);
        }
        if (!usb && wanted.load() == NABO_HOME && app.GetDeviceState() == kDeviceStateIdle &&
            !screen_off && sleep_seconds > 0 &&
            now - last_input.load() > sleep_seconds.load() * 1000000LL) {
            screen_off = true;
            Board::GetInstance().GetBacklight()->SetBrightness(0, false);
        }
        if (!audio_clocked)
            vTaskDelay(pdMS_TO_TICKS(20));
        else
            taskYIELD();
    }
}
void nabo_apps_initialize() {
    if (initialized)
        return;
    initialized = true;
    last_input = esp_timer_get_time();
    if (xTaskCreate(worker, "nabo_apps", 6144, nullptr, 5, nullptr) != pdPASS) {
        initialized = false;
        ESP_LOGE("NABO-Apps", "worker allocation failed");
    }
}

static lv_obj_t* label(lv_obj_t* parent, uint32_t color, const char* text, int x, int y,
                       int width = 220) {
    auto obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, &nabo_menu_font_16, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(obj, width);
    lv_obj_set_pos(obj, x, y);
    lv_label_set_text(obj, text);
    return obj;
}
static lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color) {
    auto obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return obj;
}
static void show_text(lv_obj_t* obj, const char* text) {
    if (strcmp(lv_label_get_text(obj), text))
        lv_label_set_text(obj, text);
}
static lv_result_t jump_info(lv_image_decoder_t*, lv_image_decoder_dsc_t* dsc,
                             lv_image_header_t* header) {
    if (dsc->src != &jump_source)
        return LV_RESULT_INVALID;
    *header = jump_source.header;
    return LV_RESULT_OK;
}
static lv_result_t jump_open(lv_image_decoder_t*, lv_image_decoder_dsc_t* dsc) {
    if (dsc->src != &jump_source || !jump_strip)
        return LV_RESULT_INVALID;
    dsc->args.no_cache = true;
    dsc->decoded = nullptr;
    return LV_RESULT_OK;
}
static lv_result_t jump_area(lv_image_decoder_t*, lv_image_decoder_dsc_t* dsc,
                             const lv_area_t* full, lv_area_t* area) {
    if (full->x1 < 0 || full->x2 >= 240 || full->y1 < 0 || full->y2 >= 320)
        return LV_RESULT_INVALID;
    int y = area->y1 == LV_COORD_MIN ? full->y1 : area->y2 + 1;
    if (y > full->y2)
        return LV_RESULT_INVALID;
    int end = std::min(y + 19, static_cast<int>(full->y2));
    auto buf = lv_draw_buf_reshape(jump_strip, LV_COLOR_FORMAT_RGB565, full->x2 - full->x1 + 1,
                                   end - y + 1, LV_STRIDE_AUTO);
    if (!buf)
        return LV_RESULT_INVALID;
    int64_t draw_start = esp_timer_get_time();
    for (int row = y; row <= end; ++row)
        nabo_jump_draw_row(
            &jump_frame, jump_frame_time, row, full->x1, full->x2,
            reinterpret_cast<uint16_t*>(buf->data) + (row - y) * buf->header.stride / 2);
    jump_draw_us += esp_timer_get_time() - draw_start;
    *area = {full->x1, y, full->x2, end};
    dsc->decoded = buf;
    return LV_RESULT_OK;
}
// The menu panel exists only while a menu page is shown: LVGL allocates from
// the C3's system heap, which XiaoZhi's TLS and Opus need on the NABO home page.
static void build_panel() {
    if (panel)
        return;
    panel = lv_obj_create(app_screen);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 240, 320);
    lv_obj_set_style_bg_color(panel, lv_color_hex(kMenuBg), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    title = label(panel, kMenuText, "工具", 16, 13, 150);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_height(title, 20);
    tag = lv_label_create(panel);
    lv_obj_set_style_text_font(tag, &nabo_mono_font_12, 0);
    lv_obj_set_style_text_color(tag, lv_color_hex(kMenuBg), 0);
    lv_obj_set_style_bg_color(tag, lv_color_hex(kMenuAccent), 0);
    lv_obj_set_style_bg_opa(tag, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tag, 3, 0);
    lv_obj_set_style_pad_hor(tag, 5, 0);
    lv_obj_set_style_pad_ver(tag, 2, 0);
    lv_label_set_text(tag, "TOOLS");
    lv_obj_align(tag, LV_ALIGN_TOP_RIGHT, -14, 15);
    auto rule = box(panel, 16, 40, 208, 1, kMenuRule);
    (void)rule;
    box(panel, 16, 40, 28, 2, kMenuAccent);
    card = box(panel, 8, 50, 224, 52, kMenuCard);
    lv_obj_set_style_radius(card, 8, 0);
    card_title = label(card, kMenuText, "", 12, 7, 200);
    lv_label_set_long_mode(card_title, LV_LABEL_LONG_DOT);
    lv_obj_set_height(card_title, 20);
    card_sub = label(card, kMenuAccent, "", 12, 27, 200);
    lv_label_set_long_mode(card_sub, LV_LABEL_LONG_DOT);
    lv_obj_set_height(card_sub, 20);
    lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    selection = box(panel, 8, kRowTop - 5, 220, 28, kMenuSelect);
    lv_obj_set_style_radius(selection, 6, 0);
    auto accent = box(selection, 0, 6, 3, 16, kMenuAccent);
    lv_obj_set_style_radius(accent, 2, 0);
    for (int i = 0; i < 7; ++i) {
        icons[i] = label(panel, kMenuMuted, "", 18, kRowTop + 1 + i * kRowPitch, 20);
        lv_obj_set_style_text_font(icons[i], &lv_font_montserrat_14, 0);
        rows[i] = label(panel, kMenuText, "", 42, kRowTop + i * kRowPitch, 132);
        lv_label_set_long_mode(rows[i], LV_LABEL_LONG_DOT);
        lv_obj_set_height(rows[i], 20);
        values[i] = label(panel, kMenuAccent, "", 150, kRowTop + i * kRowPitch, 72);
        lv_label_set_long_mode(values[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(values[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_height(values[i], 20);
    }
    scroll_track = box(panel, 232, kRowTop - 4, 2, 7 * kRowPitch - 4, kMenuRule);
    scroll_thumb = box(panel, 232, kRowTop - 4, 2, 30, kMenuAccent);
    meter = box(panel, 16, 150, 208, 14, kMenuCard);
    lv_obj_set_style_radius(meter, 7, 0);
    meter_fill = box(meter, 0, 0, 0, 14, kMenuAccent);
    lv_obj_set_style_radius(meter_fill, 7, 0);
    lv_obj_add_flag(meter, LV_OBJ_FLAG_HIDDEN);
    body = label(panel, kMenuText, "", 16, 56, 208);
    lv_obj_set_style_text_line_space(body, 6, 0);
    footer_rule = box(panel, 16, 276, 208, 1, kMenuRule);
    footer = label(panel, kMenuMuted, "", 16, 282, 214);
}
static void free_panel() {
    if (!panel)
        return;
    lv_obj_delete(panel);
    panel = nullptr;
}
void nabo_apps_build(lv_obj_t* screen) {
    if (app_screen)
        return;
    app_screen = screen;
    nabo_dice_ui_build(screen, &nabo_menu_font_16);
    nabo_muyu_ui_build(screen, &nabo_menu_font_16);
    jump_panel = lv_obj_create(screen);
    lv_obj_remove_style_all(jump_panel);
    lv_obj_set_size(jump_panel, 240, 320);
    lv_obj_set_style_bg_color(jump_panel, lv_color_hex(0xdfe1eb), 0);
    lv_obj_set_style_bg_opa(jump_panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(jump_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(jump_panel, LV_OBJ_FLAG_HIDDEN);
    jump_image = lv_image_create(jump_panel);
    lv_obj_set_size(jump_image, 240, 320);
    jump_source.header.magic = LV_IMAGE_HEADER_MAGIC;
    jump_source.header.cf = LV_COLOR_FORMAT_RGB565;
    jump_source.header.w = 240;
    jump_source.header.h = 320;
    jump_source.header.stride = 480;
    jump_source.data = reinterpret_cast<const uint8_t*>(&jump_source);
    auto decoder = lv_image_decoder_create();
    lv_image_decoder_set_info_cb(decoder, jump_info);
    lv_image_decoder_set_open_cb(decoder, jump_open);
    lv_image_decoder_set_get_area_cb(decoder, jump_area);
    jump_score = label(jump_panel, 0x344251, "0", 16, 14, 112);
    jump_best = label(jump_panel, 0xffffff, "BEST 0", 147, 17, 79);
    lv_obj_set_style_text_font(jump_best, &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_color(jump_best, lv_color_hex(0x344251), 0);
    lv_obj_set_style_bg_opa(jump_best, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(jump_best, 7, 0);
    lv_obj_set_style_pad_all(jump_best, 5, 0);
    lv_obj_set_style_text_align(jump_best, LV_TEXT_ALIGN_CENTER, 0);
    jump_station = label(jump_panel, 0x798295, "下一站 · NABO 靶心", 16, 70);
    jump_hint =
        label(jump_panel, 0x596779, "按住 OK 蓄力，松开起跳\n上：返回  下：换角色", 16, 286);
    lv_obj_set_style_text_font(jump_hint, &nabo_menu_font_16, 0);
    lv_obj_set_pos(jump_hint, 10, 286);
    lv_obj_set_width(jump_hint, 230);
    lv_obj_set_style_text_font(jump_score, &lv_font_montserrat_28, 0);
    nabo_apps_initialize();
    nabo_apps_register_tools();
    ESP_LOGI("NABO-Apps", "menus, dice, wooden fish, radio, keyboard and jump ready");
}
static void change_page(NaboApp next) {
    if (page == NABO_DICE)
        nabo_dice_ui_close();
    if (page == NABO_MUYU)
        nabo_muyu_ui_close();
    if (page == NABO_RADIO)
        nabo_radio_menu_close();
    if (page == NABO_RHYTHM)
        nabo_rhythm_ui_close();
    if (page == NABO_JUMP) {
        lv_obj_add_flag(jump_panel, LV_OBJ_FLAG_HIDDEN);
        if (jump_strip) {
            lv_draw_buf_destroy(jump_strip);
            jump_strip = nullptr;
        }
    }
    page = next;
    edit = -1;
    key_group = -1;
    selected = next == NABO_TOOLS ? 1 : 0;
    key_selected = 0;
    bool menu = !(next == NABO_HOME || next == NABO_DICE || next == NABO_MUYU ||
                  next == NABO_JUMP || next == NABO_RHYTHM);
    if (menu) {
        build_panel();
        lv_obj_move_foreground(panel);
    } else
        free_panel();
    if (next == NABO_DICE && !nabo_dice_ui_open()) {
        snprintf(notice, sizeof(notice), "内存不足，请退出重试");
        nabo_apps_open(NABO_TOOLS);
    }
    if (next == NABO_MUYU)
        nabo_muyu_ui_open();
    if (next == NABO_RADIO)
        nabo_radio_menu_open();
    if (next == NABO_RHYTHM &&
        !nabo_rhythm_ui_open(app_screen, &nabo_menu_font_16)) {
        snprintf(notice, sizeof(notice), "内存不足，请退出重试");
        nabo_apps_open(NABO_TOOLS);
    }
    if (next == NABO_JUMP) {
        jump_strip = lv_draw_buf_create(240, 20, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
        if (jump_strip) {
            nabo_jump_reset(&jump_game, esp_random(), best_score.load());
            jump_frame = jump_game;
            jump_frame_time = esp_timer_get_time() / 1000;
            lv_image_set_src(jump_image, &jump_source);
            lv_obj_remove_flag(jump_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(jump_panel);
            jump_last_frame = 0;
        } else {
            snprintf(notice, sizeof(notice), "内存不足，请退出重试");
            nabo_apps_open(NABO_TOOLS);
        }
    }
    ESP_LOGI("NABO-Apps", "page=%d", static_cast<int>(next));
}
// Change-only setters: every LVGL style write invalidates, and menus redraw
// from nabo_apps_tick() 25 times a second.
static void set_hidden(lv_obj_t* obj, bool hidden) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != hidden)
        lv_obj_set_flag(obj, LV_OBJ_FLAG_HIDDEN, hidden);
}
static void set_y(lv_obj_t* obj, int y) {
    if (lv_obj_get_style_y(obj, LV_PART_MAIN) != y)
        lv_obj_set_y(obj, y);
}
static void set_x(lv_obj_t* obj, int x) {
    if (lv_obj_get_style_x(obj, LV_PART_MAIN) != x)
        lv_obj_set_x(obj, x);
}
static void set_height(lv_obj_t* obj, int h) {
    if (lv_obj_get_style_height(obj, LV_PART_MAIN) != h)
        lv_obj_set_height(obj, h);
}
static void set_width(lv_obj_t* obj, int w) {
    if (lv_obj_get_style_width(obj, LV_PART_MAIN) != w)
        lv_obj_set_width(obj, w);
}
static void set_text_color(lv_obj_t* obj, uint32_t color) {
    if (lv_color_to_u32(lv_obj_get_style_text_color(obj, LV_PART_MAIN)) !=
        lv_color_to_u32(lv_color_hex(color)))
        lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}
static void set_bg_color(lv_obj_t* obj, uint32_t color) {
    if (lv_color_to_u32(lv_obj_get_style_bg_color(obj, LV_PART_MAIN)) != lv_color_to_u32(lv_color_hex(color)))
        lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
}
static void show_header(const char* heading, const char* section) {
    show_text(title, heading);
    show_text(tag, section);
}
static int first_visible(int cursor, int count, int visible) {
    int first = cursor > visible - 2 ? cursor - (visible - 2) : 0;
    if (first + visible > count)
        first = std::max(0, count - visible);
    return first;
}
static void show_scroll(int first, int count, int visible, int top) {
    bool scroll = count > visible;
    set_hidden(scroll_track, !scroll);
    set_hidden(scroll_thumb, !scroll);
    if (!scroll)
        return;
    int track = visible * kRowPitch - 4;
    int thumb = std::max(16, track * visible / count);
    set_y(scroll_track, top - 4);
    set_height(scroll_track, track);
    set_height(scroll_thumb, thumb);
    set_y(scroll_thumb, top - 4 + (track - thumb) * first / (count - visible));
}
// Rows with icons, right-aligned values, a highlighted cursor and a scrollbar.
static void display_rows(const char* const* names, const char* const* row_icons, int count,
                         int cursor, int top, int visible) {
    set_hidden(body, true);
    set_hidden(meter, true);
    int first = cursor >= 0 ? first_visible(cursor, count, visible) : 0;
    set_hidden(selection, cursor < 0);
    if (cursor >= 0) {
        set_y(selection, top - 5 + (cursor - first) * kRowPitch);
        set_bg_color(selection, edit >= 0 ? kMenuEdit : kMenuSelect);
    }
    for (int i = 0; i < 7; ++i) {
        int item = first + i;
        bool hide = i >= visible || item >= count;
        set_hidden(rows[i], hide);
        set_hidden(values[i], hide);
        set_hidden(icons[i], hide || !row_icons);
        if (hide)
            continue;
        int y = top + i * kRowPitch;
        set_y(rows[i], y);
        set_y(values[i], y);
        set_x(values[i], cursor < 0 ? 92 : 150);
        set_width(values[i], cursor < 0 ? 132 : 72);
        set_y(icons[i], y + 1);
        set_x(rows[i], row_icons ? 42 : 20);
        show_text(rows[i], names[item]);
        show_text(values[i], "");
        set_text_color(rows[i], item == cursor || cursor < 0 ? kMenuText : 0xb4bcc8);
        set_text_color(values[i], cursor < 0 ? kMenuText : kMenuAccent);
        if (row_icons) {
            show_text(icons[i], row_icons[item]);
            set_text_color(icons[i], item == cursor ? kMenuAccent : kMenuMuted);
        }
    }
    show_scroll(first, count, visible, top);
}
static void display_menu(const char* heading, const char* section, const char* const* names,
                         int count, int cursor, const char* const* row_icons = nullptr) {
    show_header(heading, section);
    set_hidden(card, true);
    display_rows(names, row_icons, count, cursor, kRowTop, 7);
}
static void display_body(const char* heading, const char* section, const char* text) {
    show_header(heading, section);
    show_text(body, text);
    set_y(body, 56);
    set_hidden(body, false);
    set_hidden(card, true);
    set_hidden(meter, true);
    set_hidden(selection, true);
    set_hidden(scroll_track, true);
    set_hidden(scroll_thumb, true);
    for (int i = 0; i < 7; ++i) {
        set_hidden(rows[i], true);
        set_hidden(values[i], true);
        set_hidden(icons[i], true);
    }
}
// Label/value rows without a cursor (device status, battery).
static void display_info(const char* heading, const char* section, const char* const* names,
                         const char* const* texts, int count) {
    show_header(heading, section);
    set_hidden(card, true);
    display_rows(names, nullptr, count, -1, kRowTop, 7);
    for (int i = 0; i < count && i < 7; ++i)
        show_text(values[i], texts[i]);
}
// Renders the radio's "> item" text menus as a status card plus real rows.
static void display_text_menu(const char* heading, const char* section, char* text) {
    if (strncmp(text, "> ", 2) && !strstr(text, "\n> ")) {
        display_body(heading, section, text);
        return;
    }
    const char* header[2] = {"", ""};
    const char* items[12];
    int headers = 0, count = 0, cursor = 0;
    for (char* line = text; line;) {
        char* next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        bool item = (line[0] == '>' || line[0] == ' ') && line[1] == ' ';
        if (item && count < 12) {
            if (line[0] == '>')
                cursor = count;
            items[count++] = line + 2;
        } else if (line[0] && !count && headers < 2)
            header[headers++] = line;
        line = next;
    }
    show_header(heading, section);
    show_text(card_title, header[0]);
    show_text(card_sub, header[1]);
    set_hidden(card, false);
    set_hidden(card_sub, !header[1][0]);
    set_height(card, header[1][0] ? 52 : 34);
    int top = header[1][0] ? 114 : 96;
    display_rows(items, nullptr, count, cursor, top, header[1][0] ? 5 : 6);
}
void nabo_apps_tick() {
    if (!app_screen)
        return;
    int revision = request_revision.load();
    if (revision != shown_revision && ((wanted.load() == NABO_HOME && !ready) || ready.load())) {
        shown_revision = revision;
        change_page(static_cast<NaboApp>(wanted.load()));
    }
    if (page == NABO_HOME)
        return;
    if (page == NABO_DICE) {
        nabo_dice_ui_tick();
        return;
    }
    if (page == NABO_MUYU) {
        nabo_muyu_ui_tick();
        return;
    }
    if (page == NABO_RHYTHM) {
        nabo_rhythm_ui_tick();
        return;
    }
    if (page == NABO_JUMP) {
        uint32_t now = esp_timer_get_time() / 1000;
        auto previous = jump_game.phase;
        nabo_jump_tick(&jump_game, now);
        jump_round_over = jump_game.phase == JUMP_OVER;
        if (jump_game.sound) {
            jump_game.sound = false;
            jump_sound = jump_game.pending_charge && !jump_game.pending_released ? 5
                         : jump_game.centered                                    ? 2
                                                                                 : 3;
        }
        if (previous == JUMP_SCROLLING && jump_game.phase == JUMP_FLYING)
            jump_sound = 1;
        if (jump_game.phase == JUMP_FALLING && previous != JUMP_FALLING)
            jump_sound = 4;
        if (!jump_test_mode && jump_game.best > best_score.load()) {
            best_score = jump_game.best;
            best_dirty = true;
            best_changed_at = esp_timer_get_time();
        }
        char text[80];
        snprintf(text, sizeof(text), "%lu", (unsigned long)jump_game.score);
        show_text(jump_score, text);
        snprintf(text, sizeof(text), "BEST\n%lu", (unsigned long)jump_game.best);
        show_text(jump_best, text);
        static const char* station_names[] = {"NABO 医院", "NABO 靶心", "床旁书房",
                                              "功德木鱼",  "NABO 播客", "值班咖啡"};
        snprintf(text, sizeof(text), "下一站 · %s", station_names[jump_game.next_skin]);
        show_text(jump_station, text);
        show_text(jump_hint, jump_game.phase == JUMP_OVER ? "游戏结束  OK：再来一局\n上：返回"
                             : jump_game.phase == JUMP_SCROLLING && jump_game.centered
                                 ? "正中！连击加分\n上：返回  下：换角色"
                                 : "按住 OK 蓄力，松开起跳\n上：返回  下：换角色");
        if (esp_timer_get_time() - jump_last_frame >= 14000) {
            jump_last_frame = esp_timer_get_time();
            int x0, y0, x1, y1, a0, b0, a1, b1;
            auto bounds = nabo_jump_camera_moving(&jump_frame, jump_frame_time) ||
                                  nabo_jump_camera_moving(&jump_game, now)
                              ? nabo_jump_scene_bounds
                              : nabo_jump_character_bounds;
            bounds(&jump_frame, jump_frame_time, &x0, &y0, &x1, &y1);
            bounds(&jump_game, now, &a0, &b0, &a1, &b1);
            lv_area_t area = {std::min(x0, a0), std::min(y0, b0), std::max(x1, a1),
                              std::max(y1, b1)};
            bool charge_bar = nabo_jump_charging(&jump_frame) || nabo_jump_charging(&jump_game);
            jump_frame = jump_game;
            jump_frame_time = now;
            lv_obj_invalidate_area(jump_image, &area);
            if (charge_bar) {
                lv_area_t bar = {30, 281, 210, 286};
                lv_obj_invalidate_area(jump_image, &bar);
            }
        }
        return;
    }
    char text[640];
    const char* hint = "OK 进入 · 双击上 返回\n长按上键 回到 NABO";
    if (page == NABO_TOOLS)
        display_menu("NABO 工具", "TOOLS", tools, kToolCount, selected, tool_icons);
    else if (page == NABO_SETTINGS) {
        display_menu(edit >= 0 ? "调整设置" : "声音、显示与网络", edit >= 0 ? "EDIT" : "SETTINGS",
                     settings_names, ST_COUNT, settings_selected, settings_icons);
        int first = first_visible(settings_selected, ST_COUNT, 7);
        for (int i = 0; i < 7 && first + i < ST_COUNT; ++i) {
            int item = first + i;
            text[0] = 0;
            if (item == ST_VOLUME)
                snprintf(text, sizeof(text), "%d%%", volume.load());
            else if (item == ST_BRIGHTNESS)
                snprintf(text, sizeof(text), "%d%%", brightness.load());
            else if (item == ST_MIC)
                snprintf(text, sizeof(text), "%d dB", mic_gain.load());
            else if (item == ST_SLEEP) {
                int seconds = sleep_seconds.load();
                if (!seconds)
                    snprintf(text, sizeof(text), "不休眠");
                else if (seconds % 60)
                    snprintf(text, sizeof(text), "%d 秒", seconds);
                else
                    snprintf(text, sizeof(text), "%d 分钟", seconds / 60);
            } else if (item == ST_SPEAKER)
                snprintf(text, sizeof(text), speaker ? "开" : "关");
            else if (item == ST_WIFI)
                snprintf(text, sizeof(text), wifi_on ? "开" : "关");
            if (item == edit && text[0]) {
                char editing[48];
                snprintf(editing, sizeof(editing), "< %.32s >", text);
                show_text(values[i], editing);
            } else
                show_text(values[i], text);
        }
        if (edit >= 0)
            hint = "上/下 调整数值\nOK 保存 · 双击上 结束";
    } else if (page == NABO_RADIO) {
        nabo_radio_menu_text(text, sizeof(text));
        display_text_menu("网络电台", "RADIO", text);
        hint = "上/下 选择 · OK 确定\n双击上 上一级";
    } else if (page == NABO_KEYS) {
        if (key_group < 0) {
            const char* groups[7] = {"返回",
                                     nabo_keys_group_name(0),
                                     nabo_keys_group_name(1),
                                     nabo_keys_group_name(2),
                                     nabo_keys_group_name(3),
                                     nabo_keys_group_name(4),
                                     nabo_keys_group_name(5)};
            display_menu("电脑快捷键", "KEYS", groups, 7, key_selected);
        } else if (key_group == 6) {
            char dictate[32], translate[32], ask[32];
            nabo_key_packet_t packet;
            nabo_keys_resolve(NABO_KEY_DICTATE, &packet);
            nabo_keys_chord(&packet, nabo_keys_os(), dictate, sizeof(dictate));
            nabo_keys_resolve(NABO_KEY_TRANSLATE, &packet);
            nabo_keys_chord(&packet, nabo_keys_os(), translate, sizeof(translate));
            nabo_keys_resolve(NABO_KEY_ASK, &packet);
            nabo_keys_chord(&packet, nabo_keys_os(), ask, sizeof(ask));
            const char* names[] = {"语音输入", "翻译", "询问"};
            const char* chords[] = {dictate, translate, ask};
            display_info("按键说明", "HELP", names, chords, 3);
            show_text(body, "电脑需先安装并设置\n对应软件的快捷键");
            set_y(body, kRowTop + 3 * kRowPitch + 12);
            set_hidden(body, false);
            show_text(footer, "OK 返回连接设置");
            return;
        } else if (key_group == 5) {
            const char* connect[] = {"返回", "电脑系统", "重新连接", "按键说明"};
            display_menu("连接与按键", "KEYS", connect, 4, key_selected);
            show_text(values[1], nabo_keys_os() == NABO_KEYS_WINDOWS ? "Windows" : "Mac");
        } else {
            const char* names[9] = {"返回"};
            int count = nabo_keys_group_count(key_group);
            for (int i = 0; i < count; ++i)
                names[i + 1] = nabo_key_name(nabo_keys_group_item(key_group, i));
            display_menu(nabo_keys_group_name(key_group), "KEYS", names, count + 1, key_selected);
            int first = first_visible(key_selected, count + 1, 7);
            for (int i = 0; i < 7 && first + i < count + 1; ++i) {
                if (first + i == 0)
                    continue;
                nabo_key_packet_t packet;
                nabo_keys_resolve(nabo_keys_group_item(key_group, first + i - 1), &packet);
                nabo_keys_chord(&packet, nabo_keys_os(), text, sizeof(text));
                show_text(values[i], text);
            }
        }
        if (key_group >= 0 && key_group < 5 && key_selected > 0) {
            nabo_key_packet_t packet;
            nabo_keys_resolve(nabo_keys_group_item(key_group, key_selected - 1), &packet);
            char chord[80], status[96];
            nabo_keys_chord(&packet, nabo_keys_os(), chord, sizeof(chord));
            nabo_keys_status(status, sizeof(status));
            snprintf(text, sizeof(text), "%s\n%s", chord, status);
        } else
            nabo_keys_status(text, sizeof(text));
        show_text(footer, text);
        return;
    } else if (page == NABO_STATUS) {
        auto& wifi = WifiManager::GetInstance();
        char memory[24], ip[24], ssid[40];
        snprintf(memory, sizeof(memory), "%u KB", (unsigned)(esp_get_free_heap_size() / 1024));
        snprintf(ip, sizeof(ip), "%s", wifi.IsConnected() ? wifi.GetIpAddress().c_str() : "-");
        snprintf(ssid, sizeof(ssid), "%s", wifi.IsConnected() ? wifi.GetSsid().c_str() : "-");
        char power[16] = "-";
        if (battery.load() >= 0)
            snprintf(power, sizeof(power), "%d%%", battery.load());
        const char* names[] = {"Wi-Fi", "网络", "IP", "电量", "可用内存", "固件"};
        const char* texts[] = {wifi.IsConnected() ? "已连接" : "未连接", ssid, ip, power, memory,
                               "NABO 1.1"};
        display_info("设备状态", "STATUS", names, texts, 6);
        hint = "OK 返回 · 双击上 上一级";
    } else if (page == NABO_BATTERY) {
        int pct = battery.load();
        char level[16] = "无法读取";
        if (pct >= 0)
            snprintf(level, sizeof(level), "%d%%", pct);
        const char* names[] = {"当前电量", "电量芯片"};
        const char* texts[] = {level, "CW2017"};
        display_info("电池信息", "BATTERY", names, texts, 2);
        set_hidden(meter, pct < 0);
        if (pct >= 0) {
            set_y(meter, kRowTop + 2 * kRowPitch + 10);
            set_width(meter_fill, std::max(14, 208 * std::clamp(pct, 0, 100) / 100));
            set_bg_color(meter_fill, pct <= 20 ? 0xff5a4e : pct <= 50 ? 0xffc21a : kMenuAccent);
        }
        hint = "OK 返回 · 双击上 上一级";
    } else if (page == NABO_WIFI) {
        snprintf(text, sizeof(text), "1. 手机连接热点\n    %s\n\n2. 浏览器打开\n    %s\n\n3. 配网完成后按 OK 返回",
                 WifiManager::GetInstance().GetApSsid().c_str(),
                 WifiManager::GetInstance().GetApWebUrl().c_str());
        display_body("添加网络", "WI-FI", text);
        hint = "OK 返回设置";
    }
    show_text(footer, notice[0] ? notice : hint);
}
static void adjust(int item, int direction) {
    if (item == ST_VOLUME)
        volume = std::clamp(volume.load() + direction * 10, 0, 100);
    else if (item == ST_BRIGHTNESS)
        brightness = std::clamp(brightness.load() + direction * 10, 10, 100);
    else if (item == ST_MIC)
        mic_gain = std::clamp(mic_gain.load() + direction * 6, 0, 36);
    else if (item == ST_SLEEP) {
        static const int times[] = {0, 30, 60, 120, 300, 600};
        int i = 0;
        while (i < 5 && times[i] < sleep_seconds)
            i++;
        i = (i + (direction > 0 ? 1 : 5)) % 6;
        sleep_seconds = times[i];
    }
    setting_command = item;
}
static void return_one_level() {
    NaboApp current = static_cast<NaboApp>(wanted.load());
    if (current == NABO_HOME)
        return;
    bool settled = ready.load() && current == page;
    if (settled && current == NABO_RADIO && nabo_radio_menu_key(MUSE_MENU_LEFT))
        return;
    if (settled && current == NABO_RHYTHM && nabo_rhythm_ui_key(MUSE_MENU_LEFT))
        return;
    if (settled && current == NABO_KEYS && key_group >= 0) {
        if (key_group == 6) {
            key_group = 5;
            key_selected = 3;
        } else {
            key_selected = key_group + 1;
            key_group = -1;
        }
        return;
    }
    if (settled && current == NABO_SETTINGS && edit >= 0) {
        edit = -1;
        return;
    }
    if (current == NABO_STATUS || current == NABO_BATTERY || current == NABO_WIFI) {
        if (current == NABO_WIFI)
            WifiManager::GetInstance().StartStation();
        nabo_apps_open(static_cast<NaboApp>(detail_parent.load()));
    } else
        nabo_apps_open(current == NABO_TOOLS ? NABO_HOME : NABO_TOOLS);
}
bool nabo_apps_key(muse_menu_key_t key) {
    last_input = esp_timer_get_time();
    if (screen_off.exchange(false)) {
        Board::GetInstance().GetBacklight()->SetBrightness(brightness.load(), false);
        return true;
    }
    // Double UP returns through nested menus. Long UP keeps its direct HOME path.
    if (key == MUSE_MENU_LEFT) {
        return_one_level();
        return true;
    }
    if (!nabo_apps_active()) {
        if (key == MUSE_MENU_DOWN) {
            nabo_apps_open(NABO_TOOLS);
            return true;
        }
        if (key == MUSE_MENU_UP) {
            nabo_apps_open(NABO_STATUS);
            return true;
        }
        return false;
    }
    if (key == MUSE_MENU_BACK) {
        nabo_apps_open(NABO_HOME);
        return true;
    }
    if (!ready)
        return true;
    if (page == NABO_DICE) {
        if (!nabo_dice_ui_key(key))
            nabo_apps_open(NABO_TOOLS);
        return true;
    }
    if (page == NABO_MUYU) {
        if (!nabo_muyu_ui_key(key))
            nabo_apps_open(NABO_TOOLS);
        return true;
    }
    if (page == NABO_RADIO) {
        if (!nabo_radio_menu_key(key))
            nabo_apps_open(NABO_TOOLS);
        return true;
    }
    if (page == NABO_RHYTHM) {
        if (!nabo_rhythm_ui_key(key))
            nabo_apps_open(NABO_TOOLS);
        return true;
    }
    if (page == NABO_JUMP) {
        if (key == MUSE_MENU_UP)
            nabo_apps_open(NABO_TOOLS);
        else if (key == MUSE_MENU_DOWN)
            jump_game.character = 1 - jump_game.character;
        else if (key == MUSE_MENU_RIGHT)
            nabo_jump_reset(&jump_game, esp_random(), best_score.load());
        return true;
    }
    if (page == NABO_TOOLS) {
        if (key == MUSE_MENU_UP || key == MUSE_MENU_DOWN)
            selected = (selected + (key == MUSE_MENU_UP ? kToolCount - 1 : 1)) % kToolCount;
        else if (key == MUSE_MENU_SELECT)
            nabo_apps_open(tool_apps[selected]);
    } else if (page == NABO_SETTINGS) {
        if (key == MUSE_MENU_UP || key == MUSE_MENU_DOWN) {
            if (edit >= 0)
                adjust(edit, key == MUSE_MENU_UP ? 1 : -1);
            else
                settings_selected =
                    (settings_selected + (key == MUSE_MENU_UP ? ST_COUNT - 1 : 1)) % ST_COUNT;
        } else if (key == MUSE_MENU_SELECT) {
            if (edit >= 0) {
                edit = -1;
                return true;
            }
            int item = settings_selected;
            if (item == ST_BACK)
                nabo_apps_open(NABO_TOOLS);
            else if (item == ST_VOLUME || item == ST_BRIGHTNESS || item == ST_MIC ||
                     item == ST_SLEEP)
                edit = item;
            else if (item == ST_SPEAKER) {
                speaker = !speaker.load();
                setting_command = item;
            } else if (item == ST_WIFI) {
                wifi_on = !wifi_on.load();
                setting_command = item;
            } else if (item == ST_WIFI_ADD) {
                setting_command = item;
                nabo_apps_open(NABO_WIFI);
            } else if (item == ST_STATE)
                nabo_apps_open(NABO_STATUS);
            else if (item == ST_BATTERY)
                nabo_apps_open(NABO_BATTERY);
            else if (item == ST_SCREEN)
                setting_command = item;
            else if (item == ST_POWER)
                setting_command = item;
        }
    } else if (page == NABO_KEYS) {
        if (key_group == 6) {
            if (key == MUSE_MENU_SELECT) {
                key_group = 5;
                key_selected = 3;
            }
            return true;
        }
        int count = key_group < 0 ? 7 : key_group == 5 ? 4 : nabo_keys_group_count(key_group) + 1;
        if (key == MUSE_MENU_UP || key == MUSE_MENU_DOWN)
            key_selected = (key_selected + (key == MUSE_MENU_UP ? count - 1 : 1)) % count;
        else if (key == MUSE_MENU_SELECT) {
            if (key_selected == 0) {
                if (key_group < 0)
                    nabo_apps_open(NABO_TOOLS);
                else {
                    key_group = -1;
                    key_selected = 0;
                }
            } else if (key_group < 0) {
                key_group = key_selected - 1;
                key_selected = 0;
            } else if (key_group == 5) {
                if (key_selected == 1)
                    nabo_keys_toggle_os();
                else if (key_selected == 2)
                    nabo_keys_reconnect();
                else
                    key_group = 6;
            } else
                nabo_keys_send(nabo_keys_group_item(key_group, key_selected - 1));
        }
    } else if (key == MUSE_MENU_SELECT || key == MUSE_MENU_UP) {
        if (page == NABO_WIFI)
            WifiManager::GetInstance().StartStation();
        nabo_apps_open(NABO_SETTINGS);
    }
    return true;
}
void nabo_apps_down_pressed(uint32_t event_ms) {
    if (page == NABO_RHYTHM && ready.load() && wanted.load() == NABO_RHYTHM)
        nabo_rhythm_ui_press(1, event_ms);
}
void nabo_apps_charge(bool pressed, uint32_t event_ms) {
    last_input = esp_timer_get_time();
    if (!event_ms)
        event_ms = esp_timer_get_time() / 1000;
    if (pressed && page == NABO_RHYTHM && ready.load() && wanted.load() == NABO_RHYTHM) {
        nabo_rhythm_ui_press(0, event_ms);
        return;
    }
    if (!nabo_apps_is_jump())
        return;
    if (pressed) {
        nabo_jump_press(&jump_game, event_ms);
        jump_round_over = jump_game.phase == JUMP_OVER;
        if (nabo_jump_charging(&jump_game))
            jump_sound = 5;
    } else {
        bool charging = jump_game.phase == JUMP_CHARGING;
        bool pending = jump_game.pending_charge && !jump_game.pending_released;
        nabo_jump_release(&jump_game, event_ms);
        if (charging)
            jump_sound = 1;
        else if (pending)
            jump_sound = 6;
    }
}
void nabo_apps_register_tools() {
    auto& server = McpServer::GetInstance();
    server.AddTool(
        "self.apps.open",
        "打开工牌内置程序。app "
        "可用：跳一跳(jump)、节奏回声(rhythm)、摇骰子(dice)、功德木鱼(muyu)、网络电台(radio)、"
        "电脑快捷键(computer)"
        "、设置(settings)、工具(tools)。进入程序会暂停语音对话；双击上键返回上一级，长按上键回到 "
        "NABO "
        "恢复小智。",
        PropertyList({Property("app", kPropertyTypeString).SetMaxLength(40)}),
        [](const PropertyList& properties) -> ToolResult {
            const auto& name = properties["app"].value<std::string>();
            NaboApp app = NABO_HOME;
            if (name == "jump" || name == "跳一跳")
                app = NABO_JUMP;
            else if (name == "dice" || name == "摇骰子")
                app = NABO_DICE;
            else if (name == "muyu" || name == "木鱼" || name == "功德木鱼")
                app = NABO_MUYU;
            else if (name == "radio" || name == "网络电台" || name == "电台")
                app = NABO_RADIO;
            else if (name == "computer" || name == "电脑控制" || name == "电脑快捷键")
                app = NABO_KEYS;
            else if (name == "rhythm" || name == "节奏回声" || name == "节奏游戏")
                app = NABO_RHYTHM;
            else if (name == "settings" || name == "设置")
                app = NABO_SETTINGS;
            else if (name == "tools" || name == "工具")
                app = NABO_TOOLS;
            else
                return std::unexpected(
                    "Unknown app. Use jump, rhythm, dice, muyu, radio, computer, settings or tools.");
            nabo_apps_open(app, 900);
            return std::string("已安排打开 ") + name + "。程序中使用实体按键；长按上键返回小智。";
        });
}
bool nabo_apps_voice_command(const char* transcript) {
    if (nabo_apps_active())
        return false;
    const char* name = nabo_voice_launch_app(transcript);
    if (!name)
        return false;
    NaboApp target = !strcmp(name, "dice")       ? NABO_DICE
                     : !strcmp(name, "muyu")     ? NABO_MUYU
                     : !strcmp(name, "radio")    ? NABO_RADIO
                     : !strcmp(name, "jump")     ? NABO_JUMP
                     : !strcmp(name, "computer") ? NABO_KEYS
                     : !strcmp(name, "rhythm")   ? NABO_RHYTHM
                     : !strcmp(name, "settings") ? NABO_SETTINGS
                                                 : NABO_TOOLS;
    nabo_apps_open(target);
    ESP_LOGI("NABO-Voice", "recognized launch -> %s", name);
    return true;
}
