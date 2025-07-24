#include "csi_camera.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"

#ifdef USE_ESP32

static const char *const TAG = "csi_camera";

#define CSI_CAMERA_H_RES 640
#define CSI_CAMERA_V_RES 480
#define CSI_MIPI_CSI_LANE_BITRATE_MBPS 400
#define CSI_ISP_CLOCK_HZ 80000000
#define CSI_STREAMING_STACK_SIZE 8192
#define CSI_FRAME_QUEUE_LENGTH 2

namespace esphome {
namespace csi_camera {

CsiCamera::~CsiCamera() {
#ifdef HAS_ESP32_P4_CAMERA
  this->deinit_camera_();
#endif
}

void CsiCamera::setup() {
#ifdef HAS_ESP32_P4_CAMERA
  ESP_LOGCONFIG(TAG, "Setting up CSI Camera with ESP32-P4 MIPI-CSI...");

  this->frame_ready_semaphore_ = xSemaphoreCreateBinary();
  if (!this->frame_ready_semaphore_) {
    ESP_LOGE(TAG, "Failed to create frame ready semaphore");
    this->mark_failed();
    return;
  }

  this->frame_queue_ = xQueueCreate(CSI_FRAME_QUEUE_LENGTH, sizeof(FrameData));
  if (!this->frame_queue_) {
    ESP_LOGE(TAG, "Failed to create frame queue");
    this->mark_failed();
    return;
  }

  if (this->external_clock_pin_ > 0) {
    ESP_LOGD(TAG, "Configuring external clock on GPIO%u at %u Hz",
             this->external_clock_pin_, this->external_clock_frequency_);
  }

  if (!this->init_camera_()) {
    ESP_LOGE(TAG, "Failed to initialize camera - setup marked as failed");
    this->mark_failed();
    return;
  }

  ESP_LOGCONFIG(TAG, "CSI Camera '%s' setup completed successfully", this->name_.c_str());
#else
  ESP_LOGE(TAG, "ESP32-P4 MIPI-CSI API not available - CSI Camera component disabled");
  this->mark_failed();
#endif
}

void CsiCamera::dump_config() {
  ESP_LOGCONFIG(TAG, "CSI Camera '%s':", this->name_.c_str());
  // infos de débug omises ici pour compacité
}

float CsiCamera::get_setup_priority() const {
  return setup_priority::HARDWARE - 1.0f;
}

#ifdef HAS_ESP32_P4_CAMERA
bool CsiCamera::init_ldo_() {
  if (this->ldo_initialized_) return true;

  esp_ldo_channel_config_t ldo_mipi_phy_config = {
    .chan_id = 3,
    .voltage_mv = 2500,
  };

  esp_err_t ret = esp_ldo_acquire_channel(&ldo_mipi_phy_config, &this->ldo_mipi_phy_);
  if (ret != ESP_OK) this->ldo_mipi_phy_ = nullptr;

  this->ldo_initialized_ = true;
  return true;
}

bool CsiCamera::init_sensor_() {
  if (this->sensor_initialized_) return true;

  uint8_t test_data;
  this->read_byte(0x00, &test_data);  // Lecture simple
  this->sensor_initialized_ = true;
  return true;
}

bool CsiCamera::init_camera_() {
  if (this->camera_initialized_) return true;

  if (!this->init_ldo_()) return false;

  if (this->reset_pin_) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(false);
    delayMicroseconds(10000);
    this->reset_pin_->digital_write(true);
    delayMicroseconds(10000);
  }

  if (!this->init_sensor_()) return false;

  this->frame_buffer_size_ = CSI_CAMERA_H_RES * CSI_CAMERA_V_RES * 2;
  this->frame_buffer_ = heap_caps_malloc(this->frame_buffer_size_, MALLOC_CAP_SPIRAM);
  if (!this->frame_buffer_) this->frame_buffer_ = heap_caps_malloc(this->frame_buffer_size_, MALLOC_CAP_DMA);
  if (!this->frame_buffer_) return false;

  esp_cam_ctlr_csi_config_t csi_config = {};
  csi_config.ctlr_id = 0;
  csi_config.h_res = CSI_CAMERA_H_RES;
  csi_config.v_res = CSI_CAMERA_V_RES;
  csi_config.lane_bit_rate_mbps = CSI_MIPI_CSI_LANE_BITRATE_MBPS;
  csi_config.input_data_color_type = CAM_CTLR_COLOR_RAW8;
  csi_config.output_data_color_type = CAM_CTLR_COLOR_RGB565;
  csi_config.data_lane_num = 2;
  csi_config.queue_items = 1;

  esp_err_t ret = esp_cam_new_csi_ctlr(&csi_config, &this->cam_handle_);
  if (ret != ESP_OK) return false;

  esp_cam_ctlr_evt_cbs_t cbs = {
    .on_get_new_trans = CsiCamera::camera_get_new_vb_callback,
    .on_trans_finished = CsiCamera::camera_get_finished_trans_callback,
  };

  ret = esp_cam_ctlr_register_event_callbacks(this->cam_handle_, &cbs, this);
  if (ret != ESP_OK) return false;

  ret = esp_cam_ctlr_enable(this->cam_handle_);
  if (ret != ESP_OK) return false;

  esp_isp_processor_cfg_t isp_config = {};
  isp_config.clk_hz = CSI_ISP_CLOCK_HZ;
  isp_config.input_data_source = ISP_INPUT_DATA_SOURCE_CSI;
  isp_config.input_data_color_type = ISP_COLOR_RAW8;
  isp_config.output_data_color_type = ISP_COLOR_RGB565;
  isp_config.h_res = CSI_CAMERA_H_RES;
  isp_config.v_res = CSI_CAMERA_V_RES;

  ret = esp_isp_new_processor(&isp_config, &this->isp_proc_);
  if (ret != ESP_OK) return false;

  ret = esp_isp_enable(this->isp_proc_);
  if (ret != ESP_OK) return false;

  memset(this->frame_buffer_, 0xFF, this->frame_buffer_size_);
  esp_cache_msync(this->frame_buffer_, this->frame_buffer_size_, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

  ret = esp_cam_ctlr_start(this->cam_handle_);
  if (ret != ESP_OK) return false;

  this->camera_initialized_ = true;
  return true;
}

void CsiCamera::deinit_camera_() {
  if (this->streaming_active_) this->stop_streaming();

  if (this->camera_initialized_) {
    if (this->cam_handle_) {
      esp_cam_ctlr_stop(this->cam_handle_);
      esp_cam_ctlr_disable(this->cam_handle_);
      esp_cam_ctlr_del(this->cam_handle_);
      this->cam_handle_ = nullptr;
    }

    if (this->isp_proc_) {
      esp_isp_disable(this->isp_proc_);
      esp_isp_del_processor(this->isp_proc_);
      this->isp_proc_ = nullptr;
    }

    if (this->frame_buffer_) {
      heap_caps_free(this->frame_buffer_);
      this->frame_buffer_ = nullptr;
    }

    if (this->ldo_mipi_phy_) {
      esp_ldo_release_channel(this->ldo_mipi_phy_);
      this->ldo_mipi_phy_ = nullptr;
    }

    this->camera_initialized_ = false;
    this->sensor_initialized_ = false;
    this->ldo_initialized_ = false;
  }

  if (this->frame_ready_semaphore_) {
    vSemaphoreDelete(this->frame_ready_semaphore_);
    this->frame_ready_semaphore_ = nullptr;
  }

  if (this->frame_queue_) {
    vQueueDelete(this->frame_queue_);
    this->frame_queue_ = nullptr;
  }
}

bool CsiCamera::camera_get_new_vb_callback(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data) {
  CsiCamera *camera = static_cast<CsiCamera *>(user_data);
  trans->buffer = camera->frame_buffer_;
  trans->buflen = camera->frame_buffer_size_;
  return false;
}

bool CsiCamera::camera_get_finished_trans_callback(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data) {
  CsiCamera *camera = static_cast<CsiCamera *>(user_data);
  if (camera->streaming_active_) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(camera->frame_ready_semaphore_, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
  return false;
}

bool CsiCamera::take_snapshot() {
  ESP_LOGW(TAG, "Snapshot not supported in this version (use streaming)");
  return false;
}

bool CsiCamera::start_streaming() {
  if (!this->camera_initialized_ || this->streaming_active_) return false;

  this->streaming_should_stop_ = false;
  this->streaming_active_ = true;

  BaseType_t result = xTaskCreate(
      CsiCamera::streaming_task,
      "csi_streaming",
      CSI_STREAMING_STACK_SIZE,
      this,
      5,
      &this->streaming_task_handle_);

  if (result != pdPASS) {
    this->streaming_active_ = false;
    return false;
  }

  return true;
}

bool CsiCamera::stop_streaming() {
  if (!this->streaming_active_) return true;

  this->streaming_should_stop_ = true;

  if (this->streaming_task_handle_) {
    xSemaphoreGive(this->frame_ready_semaphore_);
    uint32_t timeout = 0;
    while (this->streaming_active_ && timeout < 50) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      timeout++;
    }
    if (this->streaming_active_) {
      vTaskDelete(this->streaming_task_handle_);
      this->streaming_active_ = false;
    }
    this->streaming_task_handle_ = nullptr;
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
#endif

}  // namespace csi_camera
}  // namespace esphome

#endif  // USE_ESP32

