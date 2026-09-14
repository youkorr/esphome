#include "lvgl_camera_display.h"

#if defined(USE_ESP_IDF) && defined(USE_ESP32_VARIANT_ESP32P4)

#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome::lvgl_camera_display {

static const char *const TAG = "lvgl_camera_display";

// How often the frame rate reaching the canvas is reported. This is the
// component's only recurring log line.
static constexpr uint32_t STATS_INTERVAL_MS = 10000;

void LVGLCameraDisplay::setup() {
  if (this->camera_ == nullptr) {
    this->mark_failed(LOG_STR("No camera"));
    return;
  }
  if (!this->camera_->has_raw_frames()) {
    // A USB-UVC camera, or any device node already producing JPEG. There are no
    // pixels to point the widget at, only a compressed image, and decoding it
    // every frame is not something this component does.
    this->mark_failed(LOG_STR("The camera delivers JPEG, not pixels; only a MIPI-CSI sensor can drive a canvas"));
    return;
  }
  this->camera_->set_raw_frame_consumer(this);
  this->camera_->request_raw_frames(this->enabled_);
}

void LVGLCameraDisplay::set_enabled(bool enabled) {
  this->enabled_ = enabled;
  if (this->camera_ != nullptr)
    this->camera_->request_raw_frames(enabled);
  // Switching off gives the camera its buffer back, so the canvas must stop
  // drawing from it before the sensor starts writing over it again.
  if (!enabled)
    this->release_canvas_();
}

void LVGLCameraDisplay::release_canvas_() {
  lv_obj_t *canvas = (this->canvas_ == nullptr) ? nullptr : *this->canvas_;
  if (canvas != nullptr) {
    // A canvas is an image widget underneath, and clearing the source is what
    // stops LVGL redrawing from a buffer that is about to belong to the sensor
    // again. lv_canvas_set_draw_buf() has no null form to call here.
    ::lv_image_set_src(canvas, nullptr);
    lv_obj_invalidate(canvas);
  }
  // Rebuilt from the first frame that arrives next, which may have a different
  // geometry anyway.
  this->draw_buf_ready_ = false;
  this->draw_buf_.data = nullptr;
  this->stats_since_ms_ = 0;
  this->stats_frames_ = 0;
}

void LVGLCameraDisplay::dump_config() {
  ESP_LOGCONFIG(TAG,
                "LVGL Camera Display:\n"
                "  Camera: %s\n"
                "  Enabled: %s",
                this->camera_ == nullptr ? "none" : this->camera_->get_name().c_str(), YESNO(this->enabled_));
}

bool LVGLCameraDisplay::on_raw_frame(const esp_video_camera::RawFrame &frame) {
  lv_obj_t *canvas = (this->canvas_ == nullptr) ? nullptr : *this->canvas_;
  if (canvas == nullptr) {
    if (!this->warned_no_canvas_) {
      this->warned_no_canvas_ = true;
      ESP_LOGW(TAG, "The canvas does not exist yet; frames are being dropped");
    }
    return false;
  }

  if (!this->draw_buf_ready_) {
    lv_draw_buf_init(&this->draw_buf_, frame.width, frame.height, LV_COLOR_FORMAT_RGB565, frame.stride,
                     (void *) frame.data, frame.stride * frame.height);
    // Without this LVGL treats the buffer as constant image data and may skip
    // the redraw when the pointer it is given has not changed.
    lv_draw_buf_set_flag(&this->draw_buf_, LV_IMAGE_FLAGS_MODIFIABLE);
    this->draw_buf_ready_ = true;
    ESP_LOGD(TAG, "Showing %ux%u frames on the canvas", (unsigned) frame.width, (unsigned) frame.height);
  } else {
    this->draw_buf_.data = (uint8_t *) frame.data;
  }

  // Sets the canvas's own draw buffer as well as the image source behind it;
  // lv_image_set_src() alone would leave the two disagreeing.
  lv_canvas_set_draw_buf(canvas, &this->draw_buf_);
  lv_obj_invalidate(canvas);

  // What actually reaches the screen, on an interval. The rate here is the one
  // the camera sets; how much of it LVGL manages to draw is its own business,
  // and its performance monitor is the place to see that.
  this->stats_frames_++;
  const uint32_t now = App.get_loop_component_start_time();
  if (this->stats_since_ms_ == 0) {
    this->stats_since_ms_ = now;
  } else if (now - this->stats_since_ms_ >= STATS_INTERVAL_MS) {
    ESP_LOGD(TAG, "%ux%u to the canvas at %.1f fps", (unsigned) frame.width, (unsigned) frame.height,
             this->stats_frames_ * 1000.0f / (now - this->stats_since_ms_));
    this->stats_since_ms_ = now;
    this->stats_frames_ = 0;
  }

  // Keep this frame. LVGL renders from it during its own loop, which runs in
  // this same task, so it is finished with it well before the next frame
  // arrives -- and that is when the camera takes this buffer back.
  return true;
}

void LVGLCameraDisplay::on_raw_frames_stopped() {
  // The buffers are about to be unmapped, so nothing may still be reading from
  // them: neither the descriptor nor the canvas LVGL would redraw from it.
  this->release_canvas_();
}

}  // namespace esphome::lvgl_camera_display

#endif  // USE_ESP_IDF && USE_ESP32_VARIANT_ESP32P4
