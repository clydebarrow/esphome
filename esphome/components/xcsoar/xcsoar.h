#pragma once

#ifdef USE_ESP32

#include "esphome/components/display/display.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/static_task.h"

#include <XCSoarEsp32.hpp>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>

namespace esphome::xcsoar {

/// Runs XCSoar in its own task and shows the frames it renders on an ESPHome display.
///
/// The display is only ever touched from the main loop: the XCSoar task converts each frame into frame_,
/// wakes the loop and waits until loop() has copied the frame to the display.
class XCSoarComponent : public Component {
 public:
  explicit XCSoarComponent(display::Display *display) : display_(display) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  // After the display, which uses the default DATA priority
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  void set_frame_interval(uint32_t frame_interval) { this->frame_interval_ = frame_interval; }
  void set_task_stack_size(uint32_t task_stack_size) { this->task_stack_size_ = task_stack_size; }
#ifdef XCSOAR_ROM_FILE_COUNT
  /// Embed a file, e.g. a font, so XCSoar can open it below XCSoarEsp32::ROM_MOUNT_POINT.
  void add_rom_file(const char *name, const uint8_t *data, size_t size) {
    this->rom_files_.push_back({name, std::as_bytes(std::span(data, size))});
  }
#endif

 protected:
  static void task_main_(void *param);
  static void frame_ready_(void *ctx) noexcept;

  display::Display *display_;
  RAMAllocator<uint16_t> allocator_{RAMAllocator<uint16_t>::ALLOC_EXTERNAL};
  uint16_t *frame_{nullptr};
  XCSoarEsp32::Config config_{};
  StaticTask task_;
  StaticSemaphore_t frame_consumed_buffer_{};
  SemaphoreHandle_t frame_consumed_{nullptr};
  std::atomic<bool> frame_pending_{false};
  std::atomic<bool> task_stopped_{false};
  uint32_t frame_interval_{100};
  uint32_t task_stack_size_{65536};
#ifdef XCSOAR_ROM_FILE_COUNT
  StaticVector<XCSoarEsp32::RomFile, XCSOAR_ROM_FILE_COUNT> rom_files_;
#endif
};

}  // namespace esphome::xcsoar

#endif  // USE_ESP32
