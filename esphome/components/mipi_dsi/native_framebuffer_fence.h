#pragma once

#include <stdint.h>

namespace esphome::mipi_dsi {

// Main-loop-only ownership state. The ISR supplies a monotonically advancing
// completion generation, but never reads or modifies this state.
// A completion callback follows the DMA ISR's selection of the next buffer.
// Two completions AFTER replacement cover an ISR that already sampled the
// old selection. An old buffer must not be written until both have occurred.
class NativeFramebufferFence {
 public:
  static constexpr uint8_t NONE = 3;

  bool can_acquire(uint8_t index, uint32_t generation) const {
    if (index >= NONE || index == this->selected_ || (this->leased_ != NONE && this->leased_ != index))
      return false;
    return !this->retired_[index] || uint32_t(generation - this->retired_at_[index]) >= 2;
  }

  bool acquire(uint8_t index, uint32_t generation) {
    if (!this->can_acquire(index, generation))
      return false;
    this->retired_[index] = false;
    this->leased_ = index;
    return true;
  }

  bool is_acquired(uint8_t index) const { return index < NONE && this->leased_ == index; }

  // Only after draw_bitmap successfully selects the leased buffer. Sample
  // generation AFTER the selection, never before. Keep all other retirement
  // tickets intact; an earlier swap need not finish before this submission.
  bool present(uint8_t index, uint32_t generation) {
    if (!this->is_acquired(index))
      return false;
    this->retired_[this->selected_] = true;
    this->retired_at_[this->selected_] = generation;
    this->selected_ = index;
    this->leased_ = NONE;
    return true;
  }

 private:
  uint8_t selected_{0};  // IDF initially scans out framebuffer zero
  uint8_t leased_{NONE};
  bool retired_[3]{};
  uint32_t retired_at_[3]{};
};

}  // namespace esphome::mipi_dsi
