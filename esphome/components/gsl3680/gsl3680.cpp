#include "gsl3680.h"

namespace esphome {
namespace gsl3680 {

void GSL3680::setup() {

    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GSL3680_CONFIG();
    ESP_LOGI(TAG, "Initialize touch IO (I2C)");
    esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0, &tp_io_config, &this->tp_io_handle_);
    this->x_raw_max_ = this->swap_x_y_? this->get_display()->get_native_height(): this->get_display()->get_native_width() ;
    this->y_raw_max_ = this->swap_x_y_? this->get_display()->get_native_width() : this->get_display()->get_native_height();

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = this->get_display()->get_native_width(),
        .y_max = this->get_display()->get_native_height(),
        .rst_gpio_num = (gpio_num_t)this->reset_pin_->get_pin(),
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };

    ESP_LOGI(TAG, "Initialize touch controller gsl3680");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gsl3680(this->tp_io_handle_, &tp_cfg, &this->tp_));

    this->interrupt_pin_->setup();
    this->attach_interrupt_(this->interrupt_pin_, gpio::INTERRUPT_ANY_EDGE);

}

void GSL3680::update_touches() {
    uint16_t x[CONFIG_ESP_LCD_TOUCH_MAX_POINTS];
    uint16_t y[CONFIG_ESP_LCD_TOUCH_MAX_POINTS];
    uint16_t touch_strength[CONFIG_ESP_LCD_TOUCH_MAX_POINTS];
    uint8_t touch_cnt;
    esp_lcd_touch_read_data(this->tp_);
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(this->tp_, (uint16_t*)&x, (uint16_t*)&y, (uint16_t*)&touch_strength, &touch_cnt, CONFIG_ESP_LCD_TOUCH_MAX_POINTS);
    if (touchpad_pressed) {
        for (int i = 0; i < touch_cnt; i++) {
            ESP_LOGV(TAG, "GSL3680::update_touches: [%d] %dx%d - %d, %d", i, x[i], y[i], touch_strength[i], touch_cnt);
            this->add_raw_touch_position_(i, x[i], y[i]);
        }
        this->add_raw_touch_position_(0, x[0], y[0]); // Second touch coords are weird
    }
}

}
}
