#include "jd9365.h"
#include "esp_lcd_jd9365.h"

namespace esphome {
namespace jd9365 {

void JD9365::update() {
}

#define BSP_LCD_MIPI_DSI_LANE_NUM          (2)    // 2 data lanes
#define BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS (1500) // 1Gbps

IRAM_ATTR static bool notify_refresh_finish_(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx) {
    auto* cmp_ = (JD9365*)user_ctx;
    return cmp_->notify_refresh_finish();
}

bool JD9365::notify_refresh_finish() {
    BaseType_t need_yield = pdFALSE;
    xSemaphoreGiveFromISR(this->refresh_finish_, &need_yield);
    return (need_yield == pdTRUE);
}


void JD9365::setup() {
    ESP_LOGD(TAG, "JD9365::setup");

    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
    esp_lcd_dbi_io_config_t dbi_config = JD9365_PANEL_IO_DBI_CONFIG();

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &this->io_));

    esp_lcd_dpi_panel_config_t dpi_config = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);

    jd9365_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = this->reset_pin_->get_pin(),
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9365(this->io_, &panel_config, &this->handle_));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(this->handle_));
    ESP_ERROR_CHECK(esp_lcd_panel_init(this->handle_));
    this->cbs_ = {
        .on_color_trans_done = notify_refresh_finish_,
    };

    this->refresh_finish_ = xSemaphoreCreateBinary();
    esp_lcd_dpi_panel_register_event_callbacks(this->handle_, &this->cbs_, this);
    if (this->backlight_ != 0) {
        ESP_LOGD(TAG, "backlight_: %d", this->backlight_->state);
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(this->handle_, this->backlight_->state));
        this->backlight_->add_on_state_callback([this](bool is_on) {
            ESP_LOGD(TAG, "backlight_->add_on_state_callback: %d", is_on);
            esp_lcd_panel_disp_on_off(this->handle_, is_on);
        });
    }
}

void JD9365::loop() {
}

void JD9365::draw_pixel_at(int x, int y, Color color) {
  if (!this->get_clipping().inside(x, y))
    return;  // NOLINT

  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_0_DEGREES:
      break;
    case display::DISPLAY_ROTATION_90_DEGREES:
      std::swap(x, y);
      x = this->width_ - x - 1;
      break;
    case display::DISPLAY_ROTATION_180_DEGREES:
      x = this->width_ - x - 1;
      y = this->height_ - y - 1;
      break;
    case display::DISPLAY_ROTATION_270_DEGREES:
      std::swap(x, y);
      y = this->height_ - y - 1;
      break;
  }
  auto pixel = convert_big_endian(display::ColorUtil::color_to_565(color));

  this->draw_pixels_at(x, y, 1, 1, (const uint8_t *) &pixel, display::COLOR_ORDER_RGB, display::COLOR_BITNESS_565, true, 0, 0, 0);
  esphome::App.feed_wdt();
}

void JD9365::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order, display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) {
    if (w <= 0 || h <= 0) return;
    if (bitness != display::COLOR_BITNESS_565) {
        return display::Display::draw_pixels_at(x_start, y_start, w, h, ptr, order, bitness, big_endian, x_offset, y_offset, x_pad);
    }
    x_start += this->offset_x_;
    y_start += this->offset_y_;
    esp_err_t err;
    if (x_offset == 0 && x_pad == 0 && y_offset == 0) {
        // ESP_LOGD(TAG, "JD9365::draw_pixels_at: %dx%d - %dx%d, %d, %d, %d, %dx%d, %d", x_start, y_start, w, h, order, bitness, big_endian, x_offset, y_offset, x_pad);
        err = esp_lcd_panel_draw_bitmap(this->handle_, x_start, y_start, x_start + w, y_start + h, ptr);
        xSemaphoreTake(this->refresh_finish_, portMAX_DELAY);
    } else {
        // draw line by line
        auto stride = x_offset + w + x_pad;
        for (int y = 0; y != h; y++) {
            err = esp_lcd_panel_draw_bitmap(this->handle_, x_start, y + y_start, x_start + w, y + y_start + 1, ptr + ((y + y_offset) * stride + x_offset) * 2);
            xSemaphoreTake(this->refresh_finish_, portMAX_DELAY);
            if (err != ESP_OK)
                break;
        }
    }
    if (err != ESP_OK)
        esph_log_e(TAG, "esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(err));
}

int JD9365::get_width() {
    switch (this->rotation_) {
        case display::DISPLAY_ROTATION_0_DEGREES:
        case display::DISPLAY_ROTATION_180_DEGREES:
            return this->get_width_internal();
    }
    return this->get_height_internal();
}

int JD9365::get_height() {
    switch (this->rotation_) {
        case display::DISPLAY_ROTATION_0_DEGREES:
        case display::DISPLAY_ROTATION_180_DEGREES:
            return this->get_height_internal();
    }
    return this->get_width_internal();
}

}
}
