#ifdef USE_ESP32

#include "xcsoar.h"

#include "esphome/core/log.h"

#include <esp_heap_caps.h>

namespace esphome::xcsoar {

ESPHOME_LOG_TAG(TAG, "xcsoar");

static constexpr UBaseType_t TASK_PRIORITY = 2;
// XCSoar allocates with plain new; anything this size or larger goes to PSRAM
static constexpr size_t MALLOC_INTERNAL_LIMIT = 16384;

void XCSoarComponent::setup() {
  // XCSoar owns the whole screen, so the display must not draw on its own (test card, auto clear)
  this->display_->stop_poller();
  if (this->display_->get_rotation() != display::DISPLAY_ROTATION_0_DEGREES) {
    ESP_LOGW(TAG, "Display rotation forces a slow per-pixel copy of every frame");
  }

  // Note: this changes where malloc() places large blocks for the whole application
  heap_caps_malloc_extmem_enable(MALLOC_INTERNAL_LIMIT);

  const int width = this->display_->get_width();
  const int height = this->display_->get_height();
  this->frame_ = this->allocator_.allocate(width * height);
  if (this->frame_ == nullptr) {
    this->mark_failed(LOG_STR("Frame buffer allocation failed"));
    return;
  }

#ifdef XCSOAR_ROM_FILE_COUNT
  if (!XCSoarEsp32::MountRomFs(this->rom_files_)) {
    this->mark_failed(LOG_STR("Mounting embedded files failed"));
    return;
  }
#endif

  this->frame_consumed_ = xSemaphoreCreateBinaryStatic(&this->frame_consumed_buffer_);
  this->config_ = {
      .width = static_cast<unsigned>(width),
      .height = static_cast<unsigned>(height),
      .frame = this->frame_,
      .frame_ready = frame_ready_,
      .ctx = this,
      .frame_interval_ms = this->frame_interval_,
      .max_frames = 0,
  };

  if (!this->task_.create(task_main_, "xcsoar", this->task_stack_size_, this, TASK_PRIORITY, true)) {
    this->mark_failed(LOG_STR("Task creation failed"));
    return;
  }

  // loop() only runs when the task has a frame ready
  this->disable_loop();
}

void XCSoarComponent::task_main_(void *param) {
  auto *self = static_cast<XCSoarComponent *>(param);
  XCSoarEsp32::Run(self->config_);
  // Run() only returns on error; loop() frees the task
  self->task_stopped_ = true;
  self->enable_loop_soon_any_context();
  vTaskSuspend(nullptr);
}

void XCSoarComponent::frame_ready_(void *ctx) noexcept {
  auto *self = static_cast<XCSoarComponent *>(ctx);
  self->frame_pending_ = true;
  self->enable_loop_soon_any_context();
  xSemaphoreTake(self->frame_consumed_, portMAX_DELAY);
}

void XCSoarComponent::loop() {
  if (this->frame_pending_.exchange(false)) {
    this->display_->draw_pixels_at(0, 0, this->config_.width, this->config_.height,
                                   reinterpret_cast<const uint8_t *>(this->frame_), display::COLOR_BITNESS_565, false);
    xSemaphoreGive(this->frame_consumed_);
  }

  if (this->task_stopped_.exchange(false)) {
    this->task_.deallocate();
    this->status_set_error(LOG_STR("XCSoar stopped"));
  }

  this->disable_loop();
}

void XCSoarComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "XCSoar:\n"
                "  Size: %ux%u\n"
                "  Frame interval: %" PRIu32 " ms\n"
                "  Task stack size: %" PRIu32,
                this->config_.width, this->config_.height, this->frame_interval_, this->task_stack_size_);
#ifdef XCSOAR_ROM_FILE_COUNT
  for (const auto &file : this->rom_files_) {
    ESP_LOGCONFIG(TAG, "  Embedded file: %s (%zu bytes)", file.name, file.data.size());
  }
#endif
}

}  // namespace esphome::xcsoar

#endif  // USE_ESP32
