#ifdef USE_ESP32_VARIANT_ESP32P4

#include "xcsoar.h"

#include "esphome/core/log.h"

#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <esp_vfs_eventfd.h>
#include <pthread.h>

namespace esphome::xcsoar {

ESPHOME_LOG_TAG(TAG, "xcsoar");

// The ESPHome main loop runs on core 1 at priority 1; on the other core XCSoar never holds it up
static constexpr size_t TASK_PRIORITY = 1;
static constexpr int TASK_CORE = 0;
// XCSoar allocates with plain new, a great deal in small blocks (an airspace file alone fills internal RAM), so all
// of malloc() goes to PSRAM first. FreeRTOS objects and DMA buffers ask for internal RAM explicitly.
static constexpr size_t MALLOC_INTERNAL_LIMIT = 0;
static constexpr size_t XCSOAR_BYTES_PER_PIXEL = 4;
// A panel refresh takes about 16 ms; this only guards against a stopped panel
static constexpr uint32_t FRAME_BUFFER_FREE_TIMEOUT_MS = 100;
// XCSoar's event loop wakes up through an eventfd
static constexpr size_t EVENTFD_COUNT = 4;
static constexpr const char *DATA_MOUNT_POINT = "/sdcard";
static constexpr const char *DATA_PATH = "/sdcard/XCSoar";
// How long XCSoar waits for its data filesystem, e.g. an SD card that is still starting
static constexpr uint32_t DATA_READY_TIMEOUT_MS = 5000;

void XCSoarComponent::setup() {
  // XCSoar owns the whole screen, so the display must not draw on its own (test card, auto clear)
  this->display_->stop_poller();

  // Note: this changes where malloc() places large blocks for the whole application
  heap_caps_malloc_extmem_enable(MALLOC_INTERNAL_LIMIT);

  esp_vfs_eventfd_config_t eventfd_config = ESP_VFS_EVENTD_CONFIG_DEFAULT();
  eventfd_config.max_fds = EVENTFD_COUNT;
  if (esp_vfs_eventfd_register(&eventfd_config) != ESP_OK) {
    this->mark_failed(LOG_STR("eventfd registration failed"));
    return;
  }

  ppa_client_config_t ppa_config{};
  ppa_config.oper_type = PPA_OPERATION_SRM;
  ppa_config.max_pending_trans_num = 1;
  if (ppa_register_client(&ppa_config, &this->ppa_client_) != ESP_OK) {
    this->mark_failed(LOG_STR("PPA client registration failed"));
    return;
  }

  // XCSoar draws at the display's rotated size
  const int width = this->display_->get_width();
  const int height = this->display_->get_height();

#ifdef USE_XCSOAR_FRAME_BUFFERS
  this->dsi_ = static_cast<mipi_dsi::MipiDsi *>(this->display_);
  // The frame buffers hold the panel's own orientation, so the PPA does the rotation (counter-clockwise)
  this->out_width_ = this->display_->get_native_width();
  this->out_height_ = this->display_->get_native_height();
  this->ppa_rotation_ = static_cast<ppa_srm_rotation_angle_t>((4 - this->display_->get_rotation() / 90) % 4);
  if (this->dsi_->get_color_depth() == display::COLOR_BITNESS_888)
    this->ppa_out_color_mode_ = PPA_SRM_COLOR_MODE_RGB888;
  // Show from the second buffer first, so the first frame does not overwrite the buffer on screen
  this->back_buffer_ = 1;
#else
  // draw_pixels_at() applies the display's rotation itself
  this->out_width_ = width;
  this->out_height_ = height;
  // The PPA needs its output aligned to the cache line, in address and size
  this->frame_size_ = (width * height * 2 + CONFIG_CACHE_L2_CACHE_LINE_SIZE - 1) / CONFIG_CACHE_L2_CACHE_LINE_SIZE *
                      CONFIG_CACHE_L2_CACHE_LINE_SIZE;
  this->frame_ = static_cast<uint8_t *>(
      heap_caps_aligned_calloc(CONFIG_CACHE_L2_CACHE_LINE_SIZE, 1, this->frame_size_, MALLOC_CAP_SPIRAM));
  if (this->frame_ == nullptr) {
    this->mark_failed(LOG_STR("Frame buffer allocation failed"));
    return;
  }
#endif

#ifdef XCSOAR_VFS_MAX_FILES
  if (!this->data_vfs_->mount(DATA_MOUNT_POINT)) {
    this->mark_failed(LOG_STR("Mounting the data filesystem failed"));
    return;
  }
#endif

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
      .frame_ready = frame_ready_,
      .ctx = this,
#ifdef XCSOAR_VFS_MAX_FILES
      .data_path = DATA_PATH,
#else
      .data_path = nullptr,
#endif
      .dpi = this->dpi_,
      .frame_interval_ms = this->frame_interval_,
      .max_frames = 0,
  };

  // XCSoar's main thread is a pthread, because XCSoar identifies its threads with pthread_self(). The threads it
  // starts itself inherit this configuration, so their stacks are in PSRAM too.
  esp_pthread_cfg_t pthread_config = esp_pthread_get_default_config();
  pthread_config.stack_size = this->task_stack_size_;
  pthread_config.prio = TASK_PRIORITY;
  pthread_config.inherit_cfg = true;
  pthread_config.thread_name = "xcsoar";
  pthread_config.pin_to_core = TASK_CORE;
  pthread_config.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  pthread_t thread;
  if (esp_pthread_set_cfg(&pthread_config) != ESP_OK || pthread_create(&thread, nullptr, task_main_, this) != 0) {
    this->mark_failed(LOG_STR("Task creation failed"));
    return;
  }
  pthread_detach(thread);
  // Threads the main loop creates later keep the default configuration
  const esp_pthread_cfg_t default_config = esp_pthread_get_default_config();
  esp_pthread_set_cfg(&default_config);

  // loop() only runs when the task has a frame ready
  this->disable_loop();
}

void *XCSoarComponent::task_main_(void *param) {
  auto *self = static_cast<XCSoarComponent *>(param);
#ifdef XCSOAR_VFS_MAX_FILES
  if (!self->data_vfs_->wait_ready(DATA_READY_TIMEOUT_MS)) {
    ESP_LOGW(TAG, "Data filesystem not ready; XCSoar starts without it");
  }
#endif
  if (self->demo_) {
    XCSoarEsp32::RunDemo(self->config_);
  } else {
    XCSoarEsp32::Run(self->config_);
  }
  // XCSoar only returns when it quits or fails
  self->task_stopped_ = true;
  self->enable_loop_soon_any_context();
  return nullptr;
}

bool XCSoarComponent::convert_(const void *pixels, unsigned pitch, uint8_t *dest, size_t dest_size) {
  ppa_srm_oper_config_t srm{};
  srm.in.buffer = pixels;
  srm.in.pic_w = pitch / XCSOAR_BYTES_PER_PIXEL;
  srm.in.pic_h = this->config_.height;
  srm.in.block_w = this->config_.width;
  srm.in.block_h = this->config_.height;
  // XCSoar's blue, green, red, alpha bytes are ARGB8888 in little endian order
  srm.in.srm_cm = PPA_SRM_COLOR_MODE_ARGB8888;
  srm.out.buffer = dest;
  srm.out.buffer_size = dest_size;
  srm.out.pic_w = this->out_width_;
  srm.out.pic_h = this->out_height_;
  srm.out.srm_cm = this->ppa_out_color_mode_;
  srm.rotation_angle = this->ppa_rotation_;
  srm.scale_x = 1.0f;
  srm.scale_y = 1.0f;
  srm.mode = PPA_TRANS_MODE_BLOCKING;
  const esp_err_t err = ppa_do_scale_rotate_mirror(this->ppa_client_, &srm);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "PPA conversion failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

void XCSoarComponent::frame_ready_(const void *pixels, unsigned pitch, void *ctx) noexcept {
  auto *self = static_cast<XCSoarComponent *>(ctx);
#ifdef USE_XCSOAR_FRAME_BUFFERS
  // The back buffer was on screen until the panel's first refresh after the last switch
  if (self->buffer_shown_)
    self->dsi_->wait_frame_buffer_free(FRAME_BUFFER_FREE_TIMEOUT_MS);
  uint8_t *dest = self->dsi_->get_frame_buffer(self->back_buffer_);
  const size_t dest_size = self->dsi_->get_frame_buffer_size();
#else
  uint8_t *dest = self->frame_;
  const size_t dest_size = self->frame_size_;
#endif
  if (!self->convert_(pixels, pitch, dest, dest_size))
    return;  // the next frame tries again
  self->frame_pending_ = true;
  self->enable_loop_soon_any_context();
  xSemaphoreTake(self->frame_consumed_, portMAX_DELAY);
}

void XCSoarComponent::loop() {
  if (this->frame_pending_.exchange(false)) {
#ifdef USE_XCSOAR_FRAME_BUFFERS
    this->dsi_->show_frame_buffer(this->dsi_->get_frame_buffer(this->back_buffer_));
    this->back_buffer_ = (this->back_buffer_ + 1) % this->dsi_->get_frame_buffer_count();
    this->buffer_shown_ = true;
#else
    this->display_->draw_pixels_at(0, 0, this->out_width_, this->out_height_, this->frame_, display::COLOR_BITNESS_565,
                                   false);
#endif
    xSemaphoreGive(this->frame_consumed_);
  }

  if (this->task_stopped_.exchange(false))
    this->status_set_error(LOG_STR("XCSoar stopped"));

  this->disable_loop();
}

#ifdef USE_TOUCHSCREEN
void XCSoarComponent::touch(touchscreen::TouchPoint tp) {
  this->touch_id_ = tp.id;
  this->touching_ = true;
  this->touch_x_ = tp.x;
  this->touch_y_ = tp.y;
  XCSoarEsp32::InjectPointer(XCSoarEsp32::PointerAction::DOWN, tp.x, tp.y);
}

void XCSoarComponent::update(const touchscreen::TouchPoints_t &tpoints) {
  // The touchscreen reports the first position before calling touch()
  if (!this->touching_)
    return;
  for (const auto &tp : tpoints) {
    if (tp.id != this->touch_id_ || (tp.x == this->touch_x_ && tp.y == this->touch_y_))
      continue;
    this->touch_x_ = tp.x;
    this->touch_y_ = tp.y;
    XCSoarEsp32::InjectPointer(XCSoarEsp32::PointerAction::MOVE, tp.x, tp.y);
  }
}

void XCSoarComponent::release() {
  if (!this->touching_)
    return;
  this->touching_ = false;
  XCSoarEsp32::InjectPointer(XCSoarEsp32::PointerAction::UP, this->touch_x_, this->touch_y_);
}
#endif

void XCSoarComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "XCSoar:\n"
                "  Size: %ux%u\n"
                "  Frame interval: %" PRIu32 " ms\n"
                "  Task stack size: %" PRIu32 "\n"
                "  DPI: %u\n"
                "  Demo: %s\n"
                "  Output: %s, rotated %d degrees",
                this->config_.width, this->config_.height, this->frame_interval_, this->task_stack_size_, this->dpi_,
                YESNO(this->demo_),
#ifdef USE_XCSOAR_FRAME_BUFFERS
                LOG_STR_LITERAL("display frame buffers"),
#else
                LOG_STR_LITERAL("copied to the display"),
#endif
                static_cast<int>(this->ppa_rotation_) * 90);
#ifdef XCSOAR_ROM_FILE_COUNT
  for (const auto &file : this->rom_files_) {
    ESP_LOGCONFIG(TAG, "  Embedded file: %s (%zu bytes)", file.name, file.data.size());
  }
#endif
}

}  // namespace esphome::xcsoar

#endif  // USE_ESP32_VARIANT_ESP32P4
