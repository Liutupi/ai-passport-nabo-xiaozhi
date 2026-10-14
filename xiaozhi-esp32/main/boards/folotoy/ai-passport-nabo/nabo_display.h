#pragma once
#include "display/lcd_display.h"
#include <atomic>
#include "muse_pixel.h"
#include "src/draw/lv_image_decoder_private.h"

class NaboDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;
    ~NaboDisplay() override;
    void SetupUI() override;
    void SetEmotion(const char* emotion) override;
    void SetTheme(Theme* theme) override;
    void CaptureLCD();
    void ReleaseGameDrawBuffer();

private:
    lv_image_dsc_t source_ = {};
    lv_draw_buf_t* strip_ = nullptr;
    void* normal_draw_data_ = nullptr;
    uint32_t normal_draw_size_ = 0;
    lv_draw_buf_t* game_draw_buffer_ = nullptr;
    lv_draw_buf_t* game_draw_buffer_second_ = nullptr;
    std::atomic<bool> game_buffer_active_{false};
    lv_image_decoder_t* decoder_ = nullptr;
    lv_timer_t* animation_ = nullptr;
    muse_mode_t mode_ = MUSE_MODE_COUNT;
    int64_t mode_started_ = 0;
    uint32_t revision_ = 0;
    void Tick();
    void SetGameDrawBuffer(bool active);
    static lv_result_t Info(lv_image_decoder_t*, lv_image_decoder_dsc_t*, lv_image_header_t*);
    static lv_result_t Open(lv_image_decoder_t*, lv_image_decoder_dsc_t*);
    static lv_result_t Area(lv_image_decoder_t*, lv_image_decoder_dsc_t*, const lv_area_t*,
                            lv_area_t*);
};
