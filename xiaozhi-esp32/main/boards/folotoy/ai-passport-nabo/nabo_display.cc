#include "nabo_display.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "application.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "nabo_apps.h"
#include "src/display/lv_display_private.h"

namespace {
constexpr int kWidth = 224;
constexpr int kRows = 4;
}  // namespace

NaboDisplay::~NaboDisplay() {
    DisplayLockGuard lock(this);
    SetGameDrawBuffer(false);
    if (animation_)
        lv_timer_delete(animation_);
    if (emoji_image_) {
        lv_obj_delete(emoji_image_);
        emoji_image_ = nullptr;
    }
    if (decoder_)
        lv_image_decoder_delete(decoder_);
    if (strip_)
        lv_draw_buf_destroy(strip_);
}
static lv_display_flush_cb_t original_flush;
static std::atomic<bool> capture_lcd{false};
static void capture_flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    if (capture_lcd.load()) {
        int bytes = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1) * 2;
        flockfile(stdout);
        printf("\n@lcd %d %d %d %d %d\n", (int)area->x1, (int)area->y1, (int)area->x2,
               (int)area->y2, bytes);
        fflush(stdout);
        // Raw driver writes avoid stdout's CRLF expansion inside RGB565 data.
        int sent = 0;
        while (sent < bytes) {
            int n = usb_serial_jtag_write_bytes(pixels + sent, std::min(512, bytes - sent),
                                                pdMS_TO_TICKS(2000));
            if (n <= 0)
                break;
            sent += n;
        }
        printf("\n@lcd-end\n");
        fflush(stdout);
        funlockfile(stdout);
        if (area->y2 >= 319 && area->x2 >= 239)
            capture_lcd = false;
    }
    if (lv_display_flush_is_last(display))
        nabo_apps_record_frame();
    original_flush(display, area, pixels);
}
void NaboDisplay::CaptureLCD() {
    DisplayLockGuard lock(this);
    capture_lcd = true;
    lv_obj_invalidate(lv_screen_active());
}

lv_result_t NaboDisplay::Info(lv_image_decoder_t* decoder, lv_image_decoder_dsc_t* dsc,
                              lv_image_header_t* header) {
    auto self = static_cast<NaboDisplay*>(decoder->user_data);
    if (dsc->src != &self->source_)
        return LV_RESULT_INVALID;
    *header = self->source_.header;
    return LV_RESULT_OK;
}

lv_result_t NaboDisplay::Open(lv_image_decoder_t* decoder, lv_image_decoder_dsc_t* dsc) {
    auto self = static_cast<NaboDisplay*>(decoder->user_data);
    if (dsc->src != &self->source_ || !self->strip_)
        return LV_RESULT_INVALID;
    dsc->user_data = self;
    dsc->args.no_cache = true;
    dsc->decoded = nullptr;
    return LV_RESULT_OK;
}

lv_result_t NaboDisplay::Area(lv_image_decoder_t*, lv_image_decoder_dsc_t* dsc,
                              const lv_area_t* full, lv_area_t* area) {
    auto self = static_cast<NaboDisplay*>(dsc->user_data);
    if (!self || full->x1 < 0 || full->y1 < 0 || full->x2 >= kWidth || full->y2 >= kWidth ||
        full->x1 > full->x2 || full->y1 > full->y2)
        return LV_RESULT_INVALID;
    int y1 = area->y1 == LV_COORD_MIN ? full->y1 : area->y2 + 1;
    if (y1 > full->y2)
        return LV_RESULT_INVALID;
    int y2 = std::min(y1 + kRows - 1, static_cast<int>(full->y2));
    auto buf = lv_draw_buf_reshape(self->strip_, LV_COLOR_FORMAT_RGB565, full->x2 - full->x1 + 1,
                                   y2 - y1 + 1, LV_STRIDE_AUTO);
    if (!buf)
        return LV_RESULT_INVALID;
    muse_pixel_scale(reinterpret_cast<uint16_t*>(buf->data), buf->header.stride / 2, full->x1,
                     full->x2, y1, y2);
    *area = {full->x1, y1, full->x2, y2};
    dsc->decoded = buf;
    return LV_RESULT_OK;
}

void NaboDisplay::SetupUI() {
    if (setup_ui_called_)
        return;
    current_theme_ = LvglThemeManager::GetInstance().GetTheme("dark");
    SpiLcdDisplay::SetupUI();
    DisplayLockGuard lock(this);
    if (!lock || !emoji_image_ || !emoji_box_)
        return;
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_obj_set_style_bg_color(container_, lv_color_black(), 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_align(emoji_box_, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_size(emoji_box_, kWidth, kWidth);
    lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    strip_ = lv_draw_buf_create(kWidth, kRows, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (!strip_) {
        ESP_LOGE("NABO", "Failed to allocate streaming strip");
        return;
    }
    source_.header.magic = LV_IMAGE_HEADER_MAGIC;
    source_.header.cf = LV_COLOR_FORMAT_RGB565;
    source_.header.w = source_.header.h = kWidth;
    source_.header.stride = kWidth * 2;
    source_.data = reinterpret_cast<const uint8_t*>(this);
    source_.data_size = 1;
    decoder_ = lv_image_decoder_create();
    if (!decoder_) {
        ESP_LOGE("NABO", "Failed to create streaming decoder");
        return;
    }
    decoder_->user_data = this;
    lv_image_decoder_set_info_cb(decoder_, Info);
    lv_image_decoder_set_open_cb(decoder_, Open);
    lv_image_decoder_set_get_area_cb(decoder_, Area);
    muse_pixel_set_size(kWidth);
    lv_image_set_src(emoji_image_, &source_);
    lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    Tick();
    nabo_apps_build(lv_screen_active());
    original_flush = display_->flush_cb;
    lv_display_set_flush_cb(display_, capture_flush);
    animation_ = lv_timer_create(
        [](lv_timer_t* timer) { static_cast<NaboDisplay*>(lv_timer_get_user_data(timer))->Tick(); },
        40, this);
    ESP_LOGI("NABO", "Half-body streaming animation ready: 224x224, strip=%u bytes",
             static_cast<unsigned>(strip_->data_size));
}

void NaboDisplay::SetTheme(Theme*) {
    SpiLcdDisplay::SetTheme(LvglThemeManager::GetInstance().GetTheme("dark"));
}

void NaboDisplay::SetEmotion(const char* emotion) {
    if (!emotion)
        return;
    const char* clip = nullptr;
    if (!strcmp(emotion, "happy") || !strcmp(emotion, "laughing") || !strcmp(emotion, "funny"))
        clip = "happy";
    else if (!strcmp(emotion, "sad") || !strcmp(emotion, "crying"))
        clip = "sad";
    else if (!strcmp(emotion, "angry"))
        clip = "angry";
    else if (!strcmp(emotion, "sleepy"))
        clip = "tired";
    else if (!strcmp(emotion, "thinking") || !strcmp(emotion, "confused"))
        clip = "think";
    if (clip)
        muse_nabo_emote(clip, 4000);
}

void NaboDisplay::SetGameDrawBuffer(bool active) {
    if (active == (game_draw_buffer_ != nullptr))
        return;
    // The C3 has no PSRAM. Borrow a larger DMA-capable strip only while the
    // local game owns RAM; release it before voice TLS and Opus resume.
    while (display_->flushing)
        vTaskDelay(1);
    if (active) {
        int buffer_rows = 0;
        for (int rows : {48, 40, 32, 24}) {
            game_draw_buffer_ =
                lv_draw_buf_create(240, rows, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
            if (!game_draw_buffer_)
                continue;
            game_draw_buffer_second_ =
                lv_draw_buf_create(240, rows, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
            if (game_draw_buffer_second_) {
                buffer_rows = rows;
                break;
            }
            lv_draw_buf_destroy(game_draw_buffer_);
            game_draw_buffer_ = nullptr;
        }
        if (!game_draw_buffer_) {
            for (int rows : {48, 40, 32, 24}) {
                game_draw_buffer_ =
                    lv_draw_buf_create(240, rows, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
                if (game_draw_buffer_) {
                    buffer_rows = rows;
                    break;
                }
            }
        }
        if (!game_draw_buffer_)
            return;
        auto normal = lv_display_get_buf_active(display_);
        normal_draw_data_ = normal->data;
        normal_draw_size_ = normal->data_size;
        lv_display_set_buffers(display_, game_draw_buffer_->data,
                               game_draw_buffer_second_ ? game_draw_buffer_second_->data : nullptr,
                               game_draw_buffer_->data_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
        game_buffer_active_ = true;
        ESP_LOGI("NABO-Jump", "%d-row display buffers=%d enabled, heap=%u", buffer_rows,
                 game_draw_buffer_second_ ? 2 : 1, (unsigned)esp_get_free_heap_size());
    } else {
        lv_display_set_buffers(display_, normal_draw_data_, nullptr, normal_draw_size_,
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_draw_buf_destroy(game_draw_buffer_);
        if (game_draw_buffer_second_)
            lv_draw_buf_destroy(game_draw_buffer_second_);
        game_draw_buffer_second_ = nullptr;
        game_draw_buffer_ = nullptr;
        normal_draw_data_ = nullptr;
        normal_draw_size_ = 0;
        game_buffer_active_ = false;
    }
}

void NaboDisplay::ReleaseGameDrawBuffer() {
    if (!game_buffer_active_.load())
        return;
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    // No new frame can start under the display lock. Let the final DMA finish
    // before reclaiming its source, then give RAM back before another TLS/BLE owner.
    while (display_->flushing)
        vTaskDelay(1);
    SetGameDrawBuffer(false);
}

void NaboDisplay::Tick() {
    // Release the previous game's DMA strips before opening another program.
    // On entry, reserve the game's required decoder strip before borrowing
    // optional large display buffers from the same fragmented C3 SRAM.
    if (!nabo_apps_is_jump())
        SetGameDrawBuffer(false);
    nabo_apps_tick();
    if (nabo_apps_is_jump())
        SetGameDrawBuffer(true);
    // Jump keeps its measured 16 ms cadence; the rhythm playhead needs 50 Hz.
    int period = nabo_apps_is_jump() ? 16 : nabo_apps_is_fast() ? 20 : 0;
    if (animation_)
        lv_timer_set_period(animation_, period ? period : 40);
    if (display_)
        lv_timer_set_period(lv_display_get_refr_timer(display_), period ? period : 33);
    if (emoji_box_)
        lv_obj_set_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN,
                        nabo_apps_active() && Application::GetInstance().IsLocalAppActive());
    if (nabo_apps_active())
        return;
    auto state = Application::GetInstance().GetDeviceState();
    muse_mode_t mode = MUSE_MODE_IDLE;
    if (state == kDeviceStateStarting)
        mode = MUSE_MODE_BOOT;
    else if (state == kDeviceStateListening || state == kDeviceStateAudioTesting)
        mode = MUSE_MODE_LISTENING;
    else if (state == kDeviceStateSpeaking || state == kDeviceStateNotifying)
        mode = MUSE_MODE_SPEAKING;
    else if (state == kDeviceStateConnecting || state == kDeviceStateUpgrading)
        mode = MUSE_MODE_THINKING;
    else if (state == kDeviceStateFatalError)
        mode = MUSE_MODE_ERROR;
    auto now = esp_timer_get_time();
    if (mode != mode_) {
        mode_ = mode;
        mode_started_ = now;
    }
    muse_pose_t pose = {mode, static_cast<float>(now / 1000000.0),
                        static_cast<float>((now - mode_started_) / 1000000.0), 0, 0};
    muse_pixel_render(&pose);
    auto revision = muse_nabo_revision();
    if (revision != revision_) {
        revision_ = revision;
        lv_obj_invalidate(emoji_image_);
    }
}
