#ifdef USE_ESP32_VARIANT_ESP32P4

#include "xcsoar.h"

#include "esphome/core/log.h"

#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <esp_vfs_eventfd.h>

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
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

// FLARM's alarm levels by the time to close proximity, in seconds: 3 (urgent), then 2 (important); 1 (low) before
static constexpr float URGENT_ALARM_TIME = 8.0f;
static constexpr float IMPORTANT_ALARM_TIME = 12.0f;
// PFLAU AlarmType of a collision alarm with an aircraft
static constexpr int ALARM_TYPE_AIRCRAFT = 2;

// XCSoar raises traffic alarms only from FLARM's $PFLAU status. Without a FLARM UART, make one from the skyecho
// component's collision prediction: the count of targets, and the alarm for the most significant one.
static void feed_pflau(skyecho::SkyEcho *skyecho, const skyecho::Threat &threat) {
  skyecho::OwnshipT ownship;
  const bool gps = skyecho->get_ownship_position(&ownship);
  char sentence[96];
  int len;
  const auto &assessment = threat.assessment;
  if (threat.valid && assessment.alarm) {
    const int level = assessment.proximity_time <= URGENT_ALARM_TIME      ? 3
                      : assessment.proximity_time <= IMPORTANT_ALARM_TIME ? 2
                                                                          : 1;
    const float track = ownship.report.isTrackValid ? ownship.report.track : 0.0f;
    float bearing =
        fmodf(atan2f(threat.east, threat.north) * 180.0f / static_cast<float>(M_PI) - track + 540.0f, 360.0f) - 180.0f;
    len = snprintf(sentence, sizeof(sentence), "$PFLAU,%u,1,%d,1,%d,%d,%d,%d,%u,%06" PRIX32,
                   static_cast<unsigned>(threat.targets), gps ? 2 : 0, level, static_cast<int>(lroundf(bearing)),
                   ALARM_TYPE_AIRCRAFT, static_cast<int>(lroundf(threat.altitude)),
                   static_cast<unsigned>(lroundf(hypotf(threat.north, threat.east))), threat.report.address & 0xFFFFFF);
  } else {
    len = snprintf(sentence, sizeof(sentence), "$PFLAU,%u,1,%d,1,0,,0,,,", static_cast<unsigned>(threat.targets),
                   gps ? 2 : 0);
  }
  if (len <= 0 || static_cast<size_t>(len) + 5 >= sizeof(sentence))
    return;
  uint8_t checksum = 0;
  for (int i = 1; i != len; i++)
    checksum ^= static_cast<uint8_t>(sentence[i]);
  len += snprintf(sentence + len, sizeof(sentence) - len, "*%02X\r\n", checksum);
  XCSoarEsp32::Feed(FLARM_STREAM, std::as_bytes(std::span(sentence, len)));
}
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
  this->streams_.push_back({FLARM_STREAM, "FLARM"});
  if (this->skyecho_->has_flarm_uart()) {
    this->skyecho_->add_on_flarm_data_callback(
        [](const uint8_t *data, size_t len) { XCSoarEsp32::Feed(FLARM_STREAM, std::as_bytes(std::span(data, len))); });
  } else {
    this->skyecho_->add_on_threat_callback(
        [this](const skyecho::Threat &threat) { feed_pflau(this->skyecho_, threat); });
  }
#endif

  this->frame_consumed_ = xSemaphoreCreateBinaryStatic(&this->frame_consumed_buffer_);
  this->config_ = {
      .width = static_cast<unsigned>(width),
      .height = static_cast<unsigned>(height),
      .frame_ready = frame_ready_,
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
#ifdef USE_XCSOAR_BLE_SERIAL
      .ble = &this->ble_host_,
#else
      .ble = nullptr,
#endif
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

bool XCSoarComponent::convert_(const void *pixels, unsigned pitch, XCSoarEsp32::Rect rect, uint8_t *dest,
                               size_t dest_size) {
  ppa_srm_oper_config_t srm{};
  srm.in.buffer = pixels;
  const unsigned bytes_per_pixel = XCSoarEsp32::BytesPerPixel();
  srm.in.pic_w = pitch / bytes_per_pixel;
  srm.in.pic_h = this->config_.height;
  srm.in.block_offset_x = rect.x;
  srm.in.block_offset_y = rect.y;
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

void XCSoarComponent::frame_ready_(const void *pixels, unsigned pitch, const XCSoarEsp32::Rect *dirty,
                                   unsigned dirty_count, void *ctx) noexcept {
  auto *self = static_cast<XCSoarComponent *>(ctx);
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
  for (const auto &r : areas) {
    if (!self->convert_(pixels, pitch, r, dest, dest_size)) {
#ifdef USE_XCSOAR_FRAME_BUFFERS
      // This buffer may now be partly converted; the next frame converts all of it
      self->previous_dirty_[0].clear();
      self->previous_dirty_[0].push_back({0, 0, self->config_.width, self->config_.height});
#endif
      return;  // the next frame tries again
    }
  }
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

  if (this->task_stopped_.exchange(false)) {
    this->status_set_error(LOG_STR("XCSoar stopped"));
    this->stopped_callback_.call();
  }

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
