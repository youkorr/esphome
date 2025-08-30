#pragma once

#include <mutex>

#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"
#include "esphome/components/display/display.h"
#include "esphome/components/switch/switch.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "hal/lcd_types.h"
#include "hal/gpio_types.h"
#include "esp_ldo_regulator.h"

#include "esp_lcd_jd9365.h"

namespace esphome {
namespace jd9365 {

constexpr static const char *const TAG = "display.jd9365";

class JD9365 : public display::Display {
    public:
        void update() override;
        void setup() override;
        void loop() override;

        void draw_pixel_at(int x, int y, Color color) override;
        void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order, display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) override;

        display::DisplayType get_display_type() override { return display::DisplayType::DISPLAY_TYPE_COLOR; }
        int get_width_internal() override { return this->width_; }
        int get_height_internal() override { return this->height_; }
        int get_width() override;
        int get_height() override;

        void set_reset_pin(InternalGPIOPin *pin) { this->reset_pin_ = pin; }
        void set_backlight_switch(esphome::switch_::Switch* backlight) { this->backlight_ = backlight; }

        bool notify_refresh_finish();
    protected:
        InternalGPIOPin *reset_pin_{};
        esphome::switch_::Switch* backlight_ = 0;

        esp_lcd_panel_handle_t handle_{};
        esp_lcd_panel_io_handle_t io_{};
        esp_lcd_dpi_panel_event_callbacks_t cbs_{};

        SemaphoreHandle_t refresh_finish_ = NULL;
        size_t width_ = 800;
        size_t height_ = 1280;
        int16_t offset_x_{0};
        int16_t offset_y_{0};
};

}
}
