// Host-side regression test for the actual ownership state used by the driver.
#include "esphome/components/mipi_dsi/native_framebuffer_fence.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <set>
#include <tuple>

using esphome::mipi_dsi::NativeFramebufferFence;

struct Model {
  NativeFramebufferFence fence;
  uint32_t generation = 0;
  uint8_t selected = 0;
  uint8_t dma = 0;
  uint8_t sampled = 0;
  uint8_t leased = 3;
  bool in_flight = false;
};

static unsigned long visits;
static void explore(Model m, unsigned depth) {
  ++visits;
  // A lease must never overlap the actual DMA reader, nor the selection
  // already sampled by an ISR that can still restart DMA on that buffer.
  if (m.leased < 3) {
    assert(m.leased != m.dma);
    assert(!m.in_flight || m.leased != m.sampled);
    assert(m.leased != m.selected);
  }
  if (!depth)
    return;
  if (!m.in_flight) {
    auto next = m;
    next.sampled = m.selected;
    next.in_flight = true;
    explore(next, depth - 1);
  } else {
    auto next = m;
    // DMA completion ISR can be delayed after sampling the old selection.
    // It starts the next DMA read before issuing the notification.
    next.dma = m.sampled;
    next.in_flight = false;
    ++next.generation;
    explore(next, depth - 1);
  }
  if (m.leased == 3) {
    for (uint8_t i = 0; i < 3; ++i) {
      auto next = m;
      if (next.fence.acquire(i, next.generation)) {
        next.leased = i;
        explore(next, depth - 1);
      }
    }
  } else {
    auto next = m;
    assert(next.fence.present(next.leased, next.generation));
    next.selected = next.leased;
    next.leased = 3;
    explore(next, depth - 1);
  }
}

int main() {
  NativeFramebufferFence f;
  assert(!f.acquire(0, 0));
  assert(f.acquire(1, UINT32_MAX));
  assert(!f.acquire(2, UINT32_MAX));
  assert(f.present(1, UINT32_MAX));
  assert(!f.acquire(0, 0));  // one completion across wrap
  assert(f.acquire(2, 0));   // another free buffer needs no global fence
  assert(f.present(2, 0));
  assert(f.acquire(0, 1));   // two completions across wrap
  assert(f.present(0, 1));
  assert(!f.acquire(2, 2));  // one completion is not enough
  assert(f.acquire(2, 3));
  explore(Model{}, 16);
  Model wrap;
  wrap.generation = UINT32_MAX - 2;
  explore(wrap, 16);
  printf("Native framebuffer fence: %lu model states visited, ownership assertions passed\n", visits);
}
