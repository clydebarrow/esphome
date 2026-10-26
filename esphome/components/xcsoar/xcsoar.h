#pragma once

#ifdef USE_ESP32_VARIANT_ESP32P4

#include "esphome/components/display/display.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"

#ifdef USE_XCSOAR_FRAME_BUFFERS
#include "esphome/components/mipi_dsi/mipi_dsi.h"
#endif

#ifdef USE_TOUCHSCREEN
#include "esphome/components/touchscreen/touchscreen.h"
#endif

#ifdef USE_FILESYSTEM_VFS
#include "esphome/components/filesystem/vfs.h"
#endif

#ifdef USE_XCSOAR_SKYECHO
#include "esphome/components/skyecho/skyecho.h"
#endif

#include <XCSoarEsp32.hpp>

#include <driver/ppa.h>

#include <array>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>

namespace esphome::xcsoar {

/// Runs XCSoar in its own task and shows the frames it renders on an ESPHome display.
///
/// The XCSoar task converts each frame to the display's format with the PPA, then hands it to the main loop, the
/// only place the display is used. On a mipi_dsi display with two or more frame buffers the PPA writes straight
/// into a frame buffer that the panel is not showing, also rotating the frame to the display's rotation, and the
/// main loop just switches the panel to it. Otherwise the PPA writes into a buffer of this component that the main
/// loop copies to the display.
class XCSoarComponent : public Component
#ifdef USE_TOUCHSCREEN
    ,
                        public touchscreen::TouchListener
#endif
{
 public:
  explicit XCSoarComponent(display::Display *display) : display_(display) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  // After the display, which uses the default DATA priority
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  void set_frame_interval(uint32_t frame_interval) { this->frame_interval_ = frame_interval; }
  void set_task_stack_size(uint32_t task_stack_size) { this->task_stack_size_ = task_stack_size; }
  /// Run XCSoar's test scene instead of XCSoar.
  void set_demo(bool demo) { this->demo_ = demo; }
  void set_dpi(unsigned dpi) { this->dpi_ = dpi; }
  /// Called on the main loop once XCSoar has stopped (it quit, or failed to start); the display is free again.
  template<typename F> void add_on_stopped_callback(F &&callback) {
    this->stopped_callback_.add(std::forward<F>(callback));
  }
#ifdef USE_TOUCHSCREEN
  /// Pass the first finger of each touch to XCSoar, as a mouse.
  void set_touchscreen(touchscreen::Touchscreen *touchscreen) { touchscreen->register_listener(this); }
  void touch(touchscreen::TouchPoint tp) override;
  void update(const touchscreen::TouchPoints_t &tpoints) override;
  void release() override;
#endif
#ifdef USE_FILESYSTEM_VFS
  /// The filesystem XCSoar keeps its data on, in XCSoar/ below its vfs_path.
  void set_filesystem(filesystem::Filesystem *fs) { this->data_fs_ = fs; }
#endif
#ifdef USE_XCSOAR_SKYECHO
  /// Pass the SkyEcho's GDL90 data to XCSoar, and the data of its FLARM UART if it has one, each read as a device.
  void set_skyecho(skyecho::SkyEcho *skyecho) { this->skyecho_ = skyecho; }
#endif
#ifdef XCSOAR_ROM_FILE_COUNT
  /// Embed a file, e.g. a font, so XCSoar can open it below XCSoarEsp32::ROM_MOUNT_POINT.
  void add_rom_file(const char *name, const uint8_t *data, size_t size) {
    this->rom_files_.push_back({name, std::as_bytes(std::span(data, size))});
  }
#endif

  // The changed areas of a frame, as XCSoar reports them (at most 8)
  using DirtyRects = StaticVector<XCSoarEsp32::Rect, 8>;

 protected:
  static void *task_main_(void *param);
  static void frame_ready_(const void *pixels, unsigned pitch, const XCSoarEsp32::Rect *dirty, unsigned dirty_count,
                           void *ctx) noexcept;
  /// Convert the area rect of an XCSoar frame into dest with the PPA; runs on the XCSoar task.
  bool convert_(const void *pixels, unsigned pitch, XCSoarEsp32::Rect rect, uint8_t *dest, size_t dest_size);

  display::Display *display_;
  ppa_client_handle_t ppa_client_{nullptr};
  ppa_srm_rotation_angle_t ppa_rotation_{PPA_SRM_ROTATION_ANGLE_0};
  ppa_srm_color_mode_t ppa_out_color_mode_{PPA_SRM_COLOR_MODE_RGB565};
  // Size of the converted frame
  unsigned out_width_{0};
  unsigned out_height_{0};
#ifdef USE_XCSOAR_FRAME_BUFFERS
  mipi_dsi::MipiDsi *dsi_{nullptr};
  // The frame buffer the next frame is converted into; changed by loop() only while the task waits
  uint8_t back_buffer_{0};
  bool buffer_shown_{false};
  // The areas changed by the last frames, newest first: a frame buffer last got the frame that many frames ago
  std::array<DirtyRects, 2> previous_dirty_{};
#else
  uint8_t *frame_{nullptr};
  size_t frame_size_{0};
#endif
  XCSoarEsp32::Config config_{};
  StaticSemaphore_t frame_consumed_buffer_{};
  SemaphoreHandle_t frame_consumed_{nullptr};
  std::atomic<bool> frame_pending_{false};
  std::atomic<bool> task_stopped_{false};
  LazyCallbackManager<void()> stopped_callback_;
#ifdef USE_TOUCHSCREEN
  // The finger XCSoar follows, and where it was last
  uint8_t touch_id_{0};
  bool touching_{false};
  int16_t touch_x_{0};
  int16_t touch_y_{0};
#endif
  uint32_t frame_interval_{100};
  bool demo_{false};
  unsigned dpi_{0};
  uint32_t task_stack_size_{65536};
#ifdef USE_FILESYSTEM_VFS
  filesystem::Filesystem *data_fs_{nullptr};
  char data_path_[32]{};
#endif
#ifdef XCSOAR_ROM_FILE_COUNT
  StaticVector<XCSoarEsp32::RomFile, XCSOAR_ROM_FILE_COUNT> rom_files_;
#endif
#ifdef USE_XCSOAR_SKYECHO
  skyecho::SkyEcho *skyecho_{nullptr};
  // GDL90, and FLARM if the SkyEcho has a FLARM UART
  StaticVector<XCSoarEsp32::Stream, 2> streams_;
#endif
};

}  // namespace esphome::xcsoar

#endif  // USE_ESP32_VARIANT_ESP32P4
