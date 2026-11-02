#ifdef USE_ESP32_VARIANT_ESP32P4

#include "xcsoar.h"
#include "flight_upload.h"

#include "esphome/core/log.h"

#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <esp_vfs_eventfd.h>

#include <algorithm>
#include <numeric>
#include <pthread.h>

namespace esphome::xcsoar {

ESPHOME_LOG_TAG(TAG, "xcsoar");

// XCSoar's main (UI) thread runs above its background threads (drawing, calculation), which take one level less,
// the same as the ESPHome main loop on core 1. On no fixed core, they can use both.
static constexpr size_t TASK_PRIORITY = 2;
static constexpr int TASK_CORE = tskNO_AFFINITY;
// XCSoar allocates with plain new, a great deal in small blocks (an airspace file alone fills internal RAM), so all
// of malloc() goes to PSRAM first. FreeRTOS objects and DMA buffers ask for internal RAM explicitly.
static constexpr size_t MALLOC_INTERNAL_LIMIT = 0;
// A panel refresh takes about 16 ms; this only guards against a stopped panel
static constexpr uint32_t FRAME_BUFFER_FREE_TIMEOUT_MS = 100;
// XCSoar's drawing threads (the UI and the map) can fill at the same time
static constexpr uint32_t FILL_PENDING_MAX = 4;
// XCSoar's event loop wakes up through an eventfd
static constexpr size_t EVENTFD_COUNT = 4;
// XCSoar's directory below the data filesystem's vfs_path
static constexpr const char *DATA_DIR = "XCSoar";
// How long XCSoar waits for its data filesystem, e.g. an SD card that is still starting
static constexpr uint32_t DATA_READY_TIMEOUT_MS = 5000;
#ifdef USE_XCSOAR_SKYECHO
// The names XCSoar shows as the devices' ports
static constexpr const char *SKYECHO_STREAM = "SkyEcho";
static constexpr const char *FLARM_STREAM = "SkyEcho FLARM";
#endif

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
  // All the areas of a frame are started at once
  ppa_config.max_pending_trans_num = DirtyRects{}.capacity();
  ppa_event_callbacks_t ppa_callbacks{};
  ppa_callbacks.on_trans_done = on_converted_;
  if (ppa_register_client(&ppa_config, &this->ppa_client_) != ESP_OK ||
      ppa_client_register_event_callbacks(this->ppa_client_, &ppa_callbacks) != ESP_OK) {
    this->mark_failed(LOG_STR("PPA client registration failed"));
    return;
  }
  // Without it XCSoar fills with the CPU
  ppa_config.oper_type = PPA_OPERATION_FILL;
  ppa_config.max_pending_trans_num = FILL_PENDING_MAX;
  if (ppa_register_client(&ppa_config, &this->ppa_fill_client_) != ESP_OK)
    this->ppa_fill_client_ = nullptr;
  ppa_config.oper_type = PPA_OPERATION_BLEND;
  if (ppa_register_client(&ppa_config, &this->ppa_blend_client_) != ESP_OK)
    this->ppa_blend_client_ = nullptr;

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
  // Neither buffer holds a frame yet
  for (auto &list : this->previous_dirty_)
    list.push_back({0, 0, static_cast<unsigned>(width), static_cast<unsigned>(height)});
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

#ifdef USE_FILESYSTEM_VFS
  if (this->data_fs_ != nullptr)
    snprintf(this->data_path_, sizeof(this->data_path_), "%s/%s", this->data_fs_->get_vfs()->get_base_path(), DATA_DIR);
#endif
#ifdef USE_XCSOAR_FLIGHT_UPLOAD
  if (this->data_fs_ != nullptr && !start_flight_uploader(this->data_path_)) {
    ESP_LOGW(TAG, "Flight upload task creation failed");
  }
#endif

#ifdef XCSOAR_ROM_FILE_COUNT
  if (!XCSoarEsp32::MountRomFs(this->rom_files_)) {
    this->mark_failed(LOG_STR("Mounting embedded files failed"));
    return;
  }
#endif

#ifdef USE_XCSOAR_SKYECHO
  // XCSoar drops the data until its devices are open
  this->streams_.push_back({SKYECHO_STREAM, "GDL90"});
  this->skyecho_->add_on_gdl90_data_callback(
      [](const uint8_t *data, size_t len) { XCSoarEsp32::Feed(SKYECHO_STREAM, std::as_bytes(std::span(data, len))); });
  if (this->skyecho_->has_flarm_uart()) {
    this->streams_.push_back({FLARM_STREAM, "FLARM"});
    this->skyecho_->add_on_flarm_data_callback(
        [](const uint8_t *data, size_t len) { XCSoarEsp32::Feed(FLARM_STREAM, std::as_bytes(std::span(data, len))); });
  }
#endif

  this->frame_consumed_ = xSemaphoreCreateBinaryStatic(&this->frame_consumed_buffer_);
  this->frame_converted_ = xSemaphoreCreateBinaryStatic(&this->frame_converted_buffer_);
  this->config_ = {
      .width = static_cast<unsigned>(width),
      .height = static_cast<unsigned>(height),
      .frame_ready = frame_ready_,
      .frame_wait = frame_wait_,
      .ctx = this,
#ifdef USE_FILESYSTEM_VFS
      .data_path = this->data_fs_ != nullptr ? this->data_path_ : nullptr,
#else
      .data_path = nullptr,
#endif
      .dpi = this->dpi_,
#ifdef USE_XCSOAR_SKYECHO
      .streams = this->streams_,
#else
      .streams = {},
#endif
#ifdef USE_XCSOAR_WIFI_NETWORKS
      .wifi = &this->wifi_host_,
#else
      .wifi = nullptr,
#endif
      .traffic_alarm = traffic_alarm_,
      .volume = volume_,
      .fill = this->ppa_fill_client_ != nullptr ? fill_ : nullptr,
      .blend = this->ppa_blend_client_ != nullptr ? blend_ : nullptr,
#ifdef USE_XCSOAR_BLE_SERIAL
      .ble = &this->ble_host_,
#else
      .ble = nullptr,
#endif
      .touch_screen = this->touch_screen_,
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
#ifdef USE_FILESYSTEM_VFS
  if (self->data_fs_ != nullptr && !self->data_fs_->get_vfs()->wait_ready(DATA_READY_TIMEOUT_MS)) {
    ESP_LOGW(TAG, "Data filesystem not ready; XCSoar starts without it");
  }
#endif
  const bool quit = self->demo_ ? XCSoarEsp32::RunDemo(self->config_) : XCSoarEsp32::Run(self->config_);
  // XCSoar only returns when it quits or fails
  self->task_quit_ = quit;
  self->task_stopped_ = true;
  self->enable_loop_soon_any_context();
  return nullptr;
}

static bool rects_overlap(const XCSoarEsp32::Rect &a, const XCSoarEsp32::Rect &b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

static XCSoarEsp32::Rect bounding_rect(const XCSoarEsp32::Rect &a, const XCSoarEsp32::Rect &b) {
  const unsigned left = std::min(a.x, b.x), top = std::min(a.y, b.y);
  const unsigned right = std::max(a.x + a.width, b.x + b.width), bottom = std::max(a.y + a.height, b.y + b.height);
  return {left, top, right - left, bottom - top};
}

// Add rect to the list, merged with the rectangles it overlaps; when the list is full, merge it all into one
static void add_rect(XCSoarComponent::DirtyRects &list, XCSoarEsp32::Rect rect) {
  if (rect.width == 0 || rect.height == 0)
    return;
  for (size_t i = 0; i < list.size();) {
    if (rects_overlap(list[i], rect)) {
      rect = bounding_rect(rect, list[i]);
      list[i] = list[list.size() - 1];
      list.resize(list.size() - 1);
      i = 0;  // the larger rectangle may overlap one already passed
    } else {
      i++;
    }
  }
  if (list.size() == list.capacity()) {
    for (const auto &r : list)
      rect = bounding_rect(rect, r);
    list.clear();
  }
  list.push_back(rect);
}

// The PPA driver writes back and invalidates the cache of a whole picture for each operation, with the CPU's
// interrupts off. Describe the picture as only the rows the block covers, from a row whose start is a cache line
// boundary (the buffers start on one), so it syncs those rows only.
static void narrow_to_rows(const uint8_t *&buffer, unsigned pitch, unsigned &height, unsigned &y, unsigned rows) {
  static constexpr unsigned CACHE_LINE = 64;
  const unsigned step = CACHE_LINE / std::gcd(pitch, CACHE_LINE);
  const unsigned first = y / step * step;
  const unsigned last = std::min(height, (y + rows + step - 1) / step * step);
  buffer += size_t(first) * pitch;
  y -= first;
  height = last - first;
}

bool XCSoarComponent::convert_(const void *pixels, unsigned pitch, XCSoarEsp32::Rect rect, uint8_t *dest,
                               size_t dest_size) {
  ppa_srm_oper_config_t srm{};
  const unsigned bytes_per_pixel = XCSoarEsp32::BytesPerPixel();
  const auto *in = static_cast<const uint8_t *>(pixels);
  unsigned in_height = this->config_.height, in_y = rect.y;
  narrow_to_rows(in, pitch, in_height, in_y, rect.height);
  srm.in.buffer = in;
  srm.in.pic_w = pitch / bytes_per_pixel;
  srm.in.pic_h = in_height;
  srm.in.block_offset_x = rect.x;
  srm.in.block_offset_y = in_y;
  srm.in.block_w = rect.width;
  srm.in.block_h = rect.height;
  // Where the block lands after the counter-clockwise rotation
  const unsigned w = this->config_.width, h = this->config_.height;
  switch (this->ppa_rotation_) {
    case PPA_SRM_ROTATION_ANGLE_90:
      srm.out.block_offset_x = rect.y;
      srm.out.block_offset_y = w - rect.x - rect.width;
      break;
    case PPA_SRM_ROTATION_ANGLE_180:
      srm.out.block_offset_x = w - rect.x - rect.width;
      srm.out.block_offset_y = h - rect.y - rect.height;
      break;
    case PPA_SRM_ROTATION_ANGLE_270:
      srm.out.block_offset_x = h - rect.y - rect.height;
      srm.out.block_offset_y = rect.x;
      break;
    default:
      srm.out.block_offset_x = rect.x;
      srm.out.block_offset_y = rect.y;
      break;
  }
  // XCSoar draws RGB565, or blue, green, red, alpha bytes (ARGB8888 in little endian order)
  srm.in.srm_cm = bytes_per_pixel == 2 ? PPA_SRM_COLOR_MODE_RGB565 : PPA_SRM_COLOR_MODE_ARGB8888;
  const bool turned =
      this->ppa_rotation_ == PPA_SRM_ROTATION_ANGLE_90 || this->ppa_rotation_ == PPA_SRM_ROTATION_ANGLE_270;
  const unsigned out_pitch = dest_size / this->out_height_;
  const uint8_t *out = dest;
  unsigned out_height = this->out_height_, out_y = srm.out.block_offset_y;
  narrow_to_rows(out, out_pitch, out_height, out_y, turned ? rect.width : rect.height);
  srm.out.buffer = const_cast<uint8_t *>(out);
  srm.out.buffer_size = out_pitch * out_height;
  srm.out.pic_w = this->out_width_;
  srm.out.pic_h = out_height;
  srm.out.block_offset_y = out_y;
  srm.out.srm_cm = this->ppa_out_color_mode_;
  srm.rotation_angle = this->ppa_rotation_;
  srm.scale_x = 1.0f;
  srm.scale_y = 1.0f;
  srm.mode = PPA_TRANS_MODE_NON_BLOCKING;
  srm.user_data = this;
  const esp_err_t err = ppa_do_scale_rotate_mirror(this->ppa_client_, &srm);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "PPA conversion failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

bool XCSoarComponent::on_converted_(ppa_client_handle_t client, ppa_event_data_t *event, void *user_data) {
  auto *self = static_cast<XCSoarComponent *>(user_data);
  if (--self->conversions_left_ != 0)
    return false;
  BaseType_t woken = pdFALSE;
  self->conversion_done_(&woken);
  return woken == pdTRUE;
}

void XCSoarComponent::conversion_done_(BaseType_t *woken) {
  if (this->show_converted_) {
    this->frame_pending_ = true;
    this->enable_loop_soon_any_context();
  }
  if (woken != nullptr) {
    xSemaphoreGiveFromISR(this->frame_converted_, woken);
  } else {
    xSemaphoreGive(this->frame_converted_);
  }
}

void XCSoarComponent::frame_wait_(void *ctx) noexcept {
  auto *self = static_cast<XCSoarComponent *>(ctx);
  if (!self->converting_)
    return;
  xSemaphoreTake(self->frame_converted_, portMAX_DELAY);
  self->converting_ = false;
}

void XCSoarComponent::frame_ready_(const void *pixels, unsigned pitch, const XCSoarEsp32::Rect *dirty,
                                   unsigned dirty_count, void *ctx) noexcept {
  auto *self = static_cast<XCSoarComponent *>(ctx);
  // XCSoar waits before drawing, so the last conversion is finished; the last frame must be on screen before this
  // one is converted into the next buffer
  frame_wait_(self);
  if (self->frame_given_) {
    xSemaphoreTake(self->frame_consumed_, portMAX_DELAY);
    self->frame_given_ = false;
  }
  self->screen_pixels_ = static_cast<const uint8_t *>(pixels);
  self->screen_pitch_ = pitch;
  DirtyRects areas;
  for (unsigned i = 0; i != dirty_count; i++)
    add_rect(areas, dirty[i]);
#ifdef USE_XCSOAR_FRAME_BUFFERS
  // The back buffer was on screen until the panel's first refresh after the last switch
  if (self->buffer_shown_)
    self->dsi_->wait_frame_buffer_free(FRAME_BUFFER_FREE_TIMEOUT_MS);
  uint8_t *dest = self->dsi_->get_frame_buffer(self->back_buffer_);
  const size_t dest_size = self->dsi_->get_frame_buffer_size();
  // The back buffer still holds an older frame: bring in what changed since then too
  const DirtyRects current = areas;
  const unsigned older = self->dsi_->get_frame_buffer_count() - 1;
  for (unsigned i = 0; i != older && i != self->previous_dirty_.size(); i++) {
    for (const auto &r : self->previous_dirty_[i])
      add_rect(areas, r);
  }
  for (size_t i = self->previous_dirty_.size() - 1; i != 0; i--)
    self->previous_dirty_[i] = self->previous_dirty_[i - 1];
  self->previous_dirty_[0] = current;
#else
  // The frame is kept between calls
  uint8_t *dest = self->frame_;
  const size_t dest_size = self->frame_size_;
#endif
  // Held until every area is started, so the frame cannot be finished before then
  self->conversions_left_ = 1;
  bool started = true;
  for (const auto &r : areas) {
    self->conversions_left_++;
    if (!self->convert_(pixels, pitch, r, dest, dest_size)) {
      self->conversions_left_--;
#ifdef USE_XCSOAR_FRAME_BUFFERS
      // This buffer may now be partly converted; the next frame converts all of it
      self->previous_dirty_[0].clear();
      self->previous_dirty_[0].push_back({0, 0, self->config_.width, self->config_.height});
#endif
      started = false;  // the next frame tries again
      break;
    }
  }
  self->show_converted_ = started;
  self->frame_given_ = started;
  self->converting_ = true;
  if (--self->conversions_left_ == 0)
    self->conversion_done_(nullptr);
}

bool XCSoarComponent::fill_(void *buffer, unsigned pitch, unsigned height, XCSoarEsp32::Rect rect, uint32_t argb,
                            void *ctx) noexcept {
  auto *self = static_cast<XCSoarComponent *>(ctx);
  const unsigned bytes_per_pixel = XCSoarEsp32::BytesPerPixel();
  const auto *data = static_cast<const uint8_t *>(buffer);
  self->locate_in_screen_(data, pitch, height, rect.x, rect.y);
  narrow_to_rows(data, pitch, height, rect.y, rect.height);
  ppa_fill_oper_config_t fill{};
  fill.out.buffer = const_cast<uint8_t *>(data);
  fill.out.buffer_size = pitch * height;
  fill.out.pic_w = pitch / bytes_per_pixel;
  fill.out.pic_h = height;
  fill.out.block_offset_x = rect.x;
  fill.out.block_offset_y = rect.y;
  fill.out.fill_cm = bytes_per_pixel == 2 ? PPA_FILL_COLOR_MODE_RGB565 : PPA_FILL_COLOR_MODE_ARGB8888;
  fill.fill_block_w = rect.width;
  fill.fill_block_h = rect.height;
  fill.fill_argb_color.val = argb;
  fill.mode = PPA_TRANS_MODE_BLOCKING;
  // The PPA needs a buffer aligned to the cache line; XCSoar fills any other itself
  return ppa_do_fill(self->ppa_fill_client_, &fill) == ESP_OK;
}

void XCSoarComponent::locate_in_screen_(const uint8_t *&buffer, unsigned pitch, unsigned &height, unsigned &x,
                                        unsigned &y) const noexcept {
  // A window's canvas is part of the screen buffer and not aligned as the PPA needs; the screen buffer is
  const uint8_t *screen = this->screen_pixels_;
  if (screen == nullptr || pitch != this->screen_pitch_ || buffer < screen ||
      buffer >= screen + size_t(pitch) * this->config_.height)
    return;
  const size_t offset = buffer - screen;
  x += (offset % pitch) / XCSoarEsp32::BytesPerPixel();
  y += offset / pitch;
  buffer = screen;
  height = this->config_.height;
}

bool XCSoarComponent::blend_(void *dest, unsigned dest_pitch, unsigned dest_height, XCSoarEsp32::Rect rect,
                             const void *src, unsigned src_pitch, unsigned src_height, unsigned src_x, unsigned src_y,
                             unsigned alpha, bool white_transparent, void *ctx) noexcept {
  auto *self = static_cast<XCSoarComponent *>(ctx);
  const unsigned bytes_per_pixel = XCSoarEsp32::BytesPerPixel();
  const auto color_mode = bytes_per_pixel == 2 ? PPA_BLEND_COLOR_MODE_RGB565 : PPA_BLEND_COLOR_MODE_ARGB8888;
  const auto *dest_data = static_cast<const uint8_t *>(dest);
  self->locate_in_screen_(dest_data, dest_pitch, dest_height, rect.x, rect.y);
  const auto *src_data = static_cast<const uint8_t *>(src);
  self->locate_in_screen_(src_data, src_pitch, src_height, src_x, src_y);
  narrow_to_rows(dest_data, dest_pitch, dest_height, rect.y, rect.height);
  narrow_to_rows(src_data, src_pitch, src_height, src_y, rect.height);

  ppa_blend_oper_config_t blend{};
  blend.in_bg.buffer = dest_data;
  blend.in_bg.pic_w = dest_pitch / bytes_per_pixel;
  blend.in_bg.pic_h = dest_height;
  blend.in_bg.block_w = rect.width;
  blend.in_bg.block_h = rect.height;
  blend.in_bg.block_offset_x = rect.x;
  blend.in_bg.block_offset_y = rect.y;
  blend.in_bg.blend_cm = color_mode;
  blend.in_fg.buffer = src_data;
  blend.in_fg.pic_w = src_pitch / bytes_per_pixel;
  blend.in_fg.pic_h = src_height;
  blend.in_fg.block_w = rect.width;
  blend.in_fg.block_h = rect.height;
  blend.in_fg.block_offset_x = src_x;
  blend.in_fg.block_offset_y = src_y;
  blend.in_fg.blend_cm = color_mode;
  // In place: the background block is the output block
  blend.out.buffer = const_cast<uint8_t *>(dest_data);
  blend.out.buffer_size = dest_pitch * dest_height;
  blend.out.pic_w = dest_pitch / bytes_per_pixel;
  blend.out.pic_h = dest_height;
  blend.out.block_offset_x = rect.x;
  blend.out.block_offset_y = rect.y;
  blend.out.blend_cm = color_mode;
  blend.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  blend.bg_alpha_fix_val = 255;
  blend.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  blend.fg_alpha_fix_val = alpha;
  if (white_transparent) {
    // White foreground pixels (RGB565 white widens to F8FCF8 or FFFFFF) keep the background
    blend.fg_ck_en = true;
    blend.fg_ck_rgb_low_thres.val = 0xF8FCF8;
    blend.fg_ck_rgb_high_thres.val = 0xFFFFFF;
  }
  blend.mode = PPA_TRANS_MODE_BLOCKING;
  return ppa_do_blend(self->ppa_blend_client_, &blend) == ESP_OK;
}

void XCSoarComponent::traffic_alarm_(unsigned level, void *ctx) noexcept {
  auto *self = static_cast<XCSoarComponent *>(ctx);
  self->pending_alarm_ = static_cast<uint8_t>(level);
  self->enable_loop_soon_any_context();
}

void XCSoarComponent::volume_(unsigned percent, bool preview, void *ctx) noexcept {
  auto *self = static_cast<XCSoarComponent *>(ctx);
  self->pending_volume_ = static_cast<uint8_t>(std::min(percent, 100u)) | (preview ? VOLUME_PREVIEW : 0);
  self->enable_loop_soon_any_context();
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

  if (const uint8_t level = this->pending_alarm_.exchange(0); level != 0) {
    ESP_LOGI(TAG, "Traffic alarm, level %u", level);
    this->traffic_alarm_callback_.call(level);
  }

  if (const uint8_t pending = this->pending_volume_.exchange(NO_VOLUME); pending != NO_VOLUME) {
    const uint8_t percent = pending & ~VOLUME_PREVIEW;
    const bool preview = (pending & VOLUME_PREVIEW) != 0;
    ESP_LOGD(TAG, "Volume %u%%%s", percent, preview ? LOG_STR_LITERAL(", preview") : LOG_STR_LITERAL(""));
    this->volume_callback_.call(percent / 100.0f, preview);
  }

  if (this->task_stopped_.exchange(false)) {
    const bool quit = this->task_quit_;
    if (!quit)
      this->status_set_error(LOG_STR("XCSoar stopped"));
    this->stopped_callback_.call(quit);
  }

  this->disable_loop();
}

#ifdef USE_TOUCHSCREEN
void XCSoarComponent::touch(touchscreen::TouchPoint tp) {
  // The first finger of a touch; update() follows with all of them
  if (this->finger_count_ != 0)
    return;
  this->fingers_[0] = {tp.x, tp.y};
  this->finger_count_ = 1;
  XCSoarEsp32::InjectTouches({this->fingers_.data(), this->finger_count_});
}

void XCSoarComponent::update(const touchscreen::TouchPoints_t &tpoints) {
  // The touchscreen reports the first position before calling touch()
  if (this->finger_count_ == 0)
    return;
  std::array<XCSoarEsp32::TouchPosition, MAX_FINGERS> fingers{};
  size_t count = 0;
  for (const auto &tp : tpoints) {
    if (count == fingers.size())
      break;
    fingers[count++] = {tp.x, tp.y};
  }
  if (count == 0)
    return;
  if (count == this->finger_count_ && std::equal(fingers.begin(), fingers.begin() + count, this->fingers_.begin(),
                                                 [](const auto &a, const auto &b) { return a.x == b.x && a.y == b.y; }))
    return;
  this->fingers_ = fingers;
  this->finger_count_ = count;
  XCSoarEsp32::InjectTouches({this->fingers_.data(), this->finger_count_});
}

void XCSoarComponent::release() {
  if (this->finger_count_ == 0)
    return;
  this->finger_count_ = 0;
  XCSoarEsp32::InjectTouches({});
}
#endif

#ifdef USE_XCSOAR_WIFI_NETWORKS
// Called on XCSoar's threads; WifiNetworks queues the requests for the main loop.
void XCSoarComponent::set_wifi_networks(wifi_networks::WifiNetworks *wifi_networks) {
  using wifi_networks::WifiNetworks;
  this->wifi_host_ = {
      .list = [](XCSoarEsp32::WifiNetwork *dest, unsigned max, void *ctx) noexcept -> unsigned {
        std::array<wifi_networks::NetworkInfo, wifi_networks::MAX_LISTED> networks;
        const size_t n =
            static_cast<WifiNetworks *>(ctx)->get_networks(networks.data(), std::min<size_t>(max, networks.size()));
        for (size_t i = 0; i != n; i++) {
          const auto &src = networks[i];
          auto &out = dest[i];
          memcpy(out.ssid, src.ssid, sizeof(out.ssid));
          out.rssi = src.rssi;
          out.visible = src.visible;
          out.known = src.known;
          out.saved = src.saved;
          out.secured = src.secured;
          out.skyecho = src.skyecho;
          out.connected = src.connected;
        }
        return n;
      },
      .get_status =
          [](XCSoarEsp32::WifiStatus &status, void *ctx) noexcept {
            const auto src = static_cast<WifiNetworks *>(ctx)->get_status();
            static constexpr XCSoarEsp32::WifiState STATES[] = {
                XCSoarEsp32::WifiState::OFF, XCSoarEsp32::WifiState::CONNECTING, XCSoarEsp32::WifiState::CONNECTED};
            status.state = STATES[static_cast<uint8_t>(src.state)];
            status.automatic = src.automatic;
            memcpy(status.ssid, src.ssid, sizeof(status.ssid));
            snprintf(status.ip, sizeof(status.ip), "%s", src.ip);
          },
      .scan = [](void *ctx) noexcept { static_cast<WifiNetworks *>(ctx)->request_scan(); },
      .connect = [](const char *ssid, const char *password,
                    void *ctx) noexcept { static_cast<WifiNetworks *>(ctx)->connect(ssid, password); },
      .forget = [](const char *ssid, void *ctx) noexcept { static_cast<WifiNetworks *>(ctx)->forget(ssid); },
      .disconnect = [](void *ctx) noexcept { static_cast<WifiNetworks *>(ctx)->disconnect(); },
      .automatic = [](void *ctx) noexcept { static_cast<WifiNetworks *>(ctx)->set_automatic(); },
      .ctx = wifi_networks,
  };
}
#endif

#ifdef USE_XCSOAR_BLE_SERIAL
void XCSoarComponent::set_ble_serial(ble_serial::BleSerial *ble_serial) {
  using ble_serial::BleSerial;
  this->ble_host_ = {
      .start_scan = [](void *ctx) noexcept { static_cast<BleSerial *>(ctx)->start_scan(); },
      .stop_scan = [](void *ctx) noexcept { static_cast<BleSerial *>(ctx)->stop_scan(); },
      .open = [](const char *address, void *ctx) noexcept { static_cast<BleSerial *>(ctx)->open(address); },
      .close = [](const char *address, void *ctx) noexcept { static_cast<BleSerial *>(ctx)->close(address); },
      .write = [](const char *address, const std::byte *data, size_t size, void *ctx) noexcept -> size_t {
        return static_cast<BleSerial *>(ctx)->write(address, reinterpret_cast<const uint8_t *>(data), size);
      },
      .ctx = ble_serial,
  };
  ble_serial->add_on_device_callback([](const char *address, const char *name) {
    XCSoarEsp32::BleDetected(address, *name != '\0' ? name : nullptr, XCSoarEsp32::BLE_FEATURE_SERIAL);
  });
  ble_serial->add_on_connection_callback(
      [](const char *address, bool connected) { XCSoarEsp32::BleConnected(address, connected); });
  ble_serial->add_on_data_callback([](const char *address, const uint8_t *data, size_t size) {
    XCSoarEsp32::Feed(address, std::as_bytes(std::span(data, size)));
  });
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
