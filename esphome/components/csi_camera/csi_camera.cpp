#include "csi_camera.h"
#include "esphome/core/log.h"

#ifdef HAS_ESP32_P4_CAMERA

namespace esphome {
namespace csi_camera {

static const char *const TAG = "csi_camera";

CsiCamera::~CsiCamera() {
  this->stop_streaming();
  this->deinit_camera_();
}

void CsiCamera::setup() {
  ESP_LOGI(TAG, "Setting up camera '%s'", this->name_.c_str());
  this->frame_ready_semaphore_ = xSemaphoreCreateBinary();
  this->init_ldo_();
  this->init_camera_();
  this->init_sensor_();
}

void CsiCamera::dump_config() {
  ESP_LOGCONFIG(TAG, "CSI Camera '%s':", this->name_.c_str());
  ESP_LOGCONFIG(TAG, "  External Clock Pin: GPIO %u", this->external_clock_pin_);
  ESP_LOGCONFIG(TAG, "  Clock Frequency: %u Hz", this->external_clock_frequency_);
  ESP_LOGCONFIG(TAG, "  Sensor Address: 0x%02X", this->sensor_address_);
}

float CsiCamera::get_setup_priority() const {
  return setup_priority::HARDWARE;
}

bool CsiCamera::start_streaming() {
  if (!this->camera_initialized_ || this->streaming_active_) return false;

  this->streaming_should_stop_ = false;
  this->streaming_active_ = true;

  xTaskCreate(&CsiCamera::streaming_task, "csi_streaming", 8192, this, 5, &this->streaming_task_handle_);
  return true;
}

bool CsiCamera::stop_streaming() {
  if (!this->streaming_active_) return false;
  this->streaming_should_stop_ = true;
  while (this->streaming_active_) {
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  return true;
}

void CsiCamera::streaming_task(void *parameter) {
  CsiCamera *camera = static_cast<CsiCamera *>(parameter);
  camera->streaming_loop_();
}

void CsiCamera::streaming_loop_() {
  ESP_LOGD(TAG, "Streaming loop started for camera '%s'", this->name_.c_str());

  while (!this->streaming_should_stop_) {
    if (xSemaphoreTake(this->frame_ready_semaphore_, 100 / portTICK_PERIOD_MS) == pdTRUE) {
      esp_cache_msync(this->frame_buffer_, this->frame_buffer_size_, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
      this->on_frame_callbacks_.call(static_cast<uint8_t *>(this->frame_buffer_), this->frame_buffer_size_);
    } else {
      vTaskDelay(1 / portTICK_PERIOD_MS);
    }
  }

  this->streaming_active_ = false;
  ESP_LOGD(TAG, "Streaming loop ended for camera '%s'", this->name_.c_str());
  vTaskDelete(nullptr);
}

bool CsiCamera::camera_get_new_vb_callback(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t *trans, void *user_data) {
  CsiCamera *camera = static_cast<CsiCamera *>(user_data);
  trans->buffer = camera->frame_buffer_;
  trans->buflen = camera->frame_buffer_size_;
  return false;
}

bool CsiCamera::camera_get_finished_trans_callback(esp_cam_ctlr_handle_t, esp_cam_ctlr_trans_t *, void *user_data) {
  CsiCamera *camera = static_cast<CsiCamera *>(user_data);
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(camera->frame_ready_semaphore_, &xHigherPriorityTaskWoken);
  return false;
}

bool CsiCamera::init_camera_() {
  // Exemple de config minimaliste
  esp_cam_ctlr_config_t config = {};
  config.interface_type = ESP_CAM_CTLR_INTERFACE_TYPE_CSI;
  config.data_format = ESP_CAM_CTLR_DATA_FMT_UYVY;
  config.frame_size.width = 640;
  config.frame_size.height = 480;
  config.on_get_new_trans = camera_get_new_vb_callback;
  config.on_trans_finished = camera_get_finished_trans_callback;
  config.user_data = this;
  config.queue_items = 2;

  esp_err_t err = esp_cam_ctlr_new(&config, &this->cam_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize camera controller: %s", esp_err_to_name(err));
    return false;
  }

  this->frame_buffer_size_ = 640 * 480 * 2; // UYVY = 2 bytes/pixel
  this->frame_buffer_ = heap_caps_malloc(this->frame_buffer_size_, MALLOC_CAP_DMA);
  if (!this->frame_buffer_) {
    ESP_LOGE(TAG, "Failed to allocate frame buffer");
    return false;
  }

  err = esp_cam_ctlr_enable(this->cam_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable camera controller: %s", esp_err_to_name(err));
    return false;
  }

  this->camera_initialized_ = true;
  return true;
}

bool CsiCamera::init_sensor_() {
  // Placeholder pour l'initialisation I2C du capteur
  return true;
}

bool CsiCamera::init_ldo_() {
  // Placeholder pour l'activation de la LDO MIPI PHY
  return true;
}

void CsiCamera::deinit_camera_() {
  if (this->cam_handle_) {
    esp_cam_ctlr_disable(this->cam_handle_);
    esp_cam_ctlr_del(this->cam_handle_);
    this->cam_handle_ = nullptr;
  }
  if (this->frame_buffer_) {
    free(this->frame_buffer_);
    this->frame_buffer_ = nullptr;
  }
  this->camera_initialized_ = false;
}

}  // namespace csi_camera
}  // namespace esphome

#endif
