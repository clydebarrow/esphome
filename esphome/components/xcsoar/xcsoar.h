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

#ifdef USE_XCSOAR_FLARM_RECEIVER
#include "esphome/components/flarm_receiver/flarm_receiver.h"
#endif
#ifdef USE_XCSOAR_SKYECHO
#include "esphome/components/skyecho/skyecho.h"
#endif

#ifdef USE_XCSOAR_BLE_SERIAL
#include "esphome/components/ble_serial/ble_serial.h"
#endif
#ifdef USE_XCSOAR_WIFI_NETWORKS
#include "esphome/components/wifi_networks/wifi_networks.h"
#endif
#ifdef USE_XCSOAR_UART
#include "esphome/components/uart/uart.h"
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
/// The XCSoar task starts the PPA converting each frame to the display's format and goes on; when the PPA is done,
/// the frame goes to the main loop, the only place the display is used. XCSoar waits for the PPA only before it
/// draws again. On a mipi_dsi display with two or more frame buffers the PPA writes straight into a frame buffer
/// that the panel is not showing, also rotating the frame to the display's rotation, and the main loop just
/// switches the panel to it. Otherwise the PPA writes into a buffer of this component that the main loop copies
/// to the display.
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
  /// Called on the main loop once XCSoar has stopped, with quit true when the user quit and false when it failed;
  /// the display is free again.
  template<typename F> void add_on_stopped_callback(F &&callback) {
    this->stopped_callback_.add(std::forward<F>(callback));
  }
  /// Called on the main loop with the FLARM alarm level (1 low, 2 important, 3 urgent) when XCSoar raises a traffic
  /// alarm or it escalates.
  template<typename F> void add_on_traffic_alarm_callback(F &&callback) {
    this->traffic_alarm_callback_.add(std::forward<F>(callback));
  }
  /// Called on the main loop with the master volume (0 to 1) from XCSoar's Audio settings, at startup and when it is
  /// saved, and with preview set on each change while the user adjusts it, to play a short tone at that volume.
  template<typename F> void add_on_volume_callback(F &&callback) {
    this->volume_callback_.add(std::forward<F>(callback));
  }
#ifdef USE_TOUCHSCREEN
  /// Pass the first two fingers of each touch to XCSoar: the first works the mouse, two give two-finger gestures.
  void set_touchscreen(touchscreen::Touchscreen *touchscreen) {
    touchscreen->register_listener(this);
    this->touch_screen_ = true;
  }
  void touch(touchscreen::TouchPoint tp) override;
  void update(const touchscreen::TouchPoints_t &tpoints) override;
  void release() override;
#endif
#ifdef USE_FILESYSTEM_VFS
  /// The filesystem XCSoar keeps its data on, in XCSoar/ below its vfs_path.
  void set_filesystem(filesystem::Filesystem *fs) { this->data_fs_ = fs; }
#endif
#ifdef USE_XCSOAR_FLARM_RECEIVER
  /// Pass the FLARM receiver's GPS sentences and traffic to XCSoar, read as a FLARM.
  void set_flarm_receiver(flarm_receiver::FlarmReceiver *receiver) { this->flarm_receiver_ = receiver; }
#endif
#ifdef USE_XCSOAR_SKYECHO
  /// Pass the SkyEcho's GDL90 data to XCSoar, and the data of its FLARM UART if it has one, each read as a device.
  void set_skyecho(skyecho::SkyEcho *skyecho) { this->skyecho_ = skyecho; }
#endif
#ifdef XCSOAR_STREAM_COUNT
  /// Declare a stream XCSoar reads as a device, before setup(); name is also the port it shows.
  void add_stream(const char *name, const char *driver) { this->streams_.push_back({name, driver}); }
#endif
#ifdef USE_XCSOAR_WIFI_NETWORKS
  /// Let XCSoar's network settings list, join and forget Wi-Fi networks.
  void set_wifi_networks(wifi_networks::WifiNetworks *wifi_networks);
#endif
#ifdef USE_XCSOAR_BLE_SERIAL
  /// Let XCSoar find, open and use Bluetooth LE serial devices, as "BLE port" devices.
  void set_ble_serial(ble_serial::BleSerial *ble_serial);
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
  static void frame_wait_(void *ctx) noexcept;
  /// Start converting the area rect of an XCSoar frame into dest with the PPA; runs on the XCSoar task.
  bool convert_(const void *pixels, unsigned pitch, XCSoarEsp32::Rect rect, uint8_t *dest, size_t dest_size);
  /// The PPA has converted one area; called from its interrupt.
  static bool on_converted_(ppa_client_handle_t client, ppa_event_data_t *event, void *user_data);
  /// The whole frame is converted: show it, and let XCSoar draw again. woken is nullptr on the XCSoar task.
  void conversion_done_(BaseType_t *woken);
  static void traffic_alarm_(unsigned level, void *ctx) noexcept;
  static void volume_(unsigned percent, bool preview, void *ctx) noexcept;
#if defined(USE_XCSOAR_FLARM_RECEIVER) || defined(USE_XCSOAR_WIFI_NETWORKS)
  static void club_glider_(const char *type, const char *registration, void *ctx) noexcept;
#endif
  static bool fill_(void *buffer, unsigned pitch, unsigned height, XCSoarEsp32::Rect rect, uint32_t argb,
                    void *ctx) noexcept;
  /// A fill or blend is finished; called from the PPA's interrupt with the drawing thread's semaphore.
  static bool on_drawn_(ppa_client_handle_t client, ppa_event_data_t *event, void *user_data);
  /// Wait for the fill or blend just started; false, and no more of them, if the PPA does not finish it.
  bool wait_drawn_(SemaphoreHandle_t done) noexcept;
  static bool blend_(void *dest, unsigned dest_pitch, unsigned dest_height, XCSoarEsp32::Rect rect, const void *src,
                     unsigned src_pitch, unsigned src_height, unsigned src_x, unsigned src_y, unsigned alpha,
                     bool white_transparent, void *ctx) noexcept;
  /// If buffer points into the screen buffer, make it the screen buffer and move the block by the offset.
  void locate_in_screen_(const uint8_t *&buffer, unsigned pitch, unsigned &height, unsigned &x,
                         unsigned &y) const noexcept;

  display::Display *display_;
  ppa_client_handle_t ppa_client_{nullptr};
  // Fills large rectangles of XCSoar's canvases for its drawing threads
  ppa_client_handle_t ppa_fill_client_{nullptr};
  ppa_client_handle_t ppa_blend_client_{nullptr};
  // Set when a fill or blend did not finish: XCSoar then does them with the CPU
  std::atomic<bool> ppa_draw_failed_{false};
  // XCSoar's screen buffer, from the last frame: its windows' canvases point into it, unaligned
  std::atomic<const uint8_t *> screen_pixels_{nullptr};
  std::atomic<unsigned> screen_pitch_{0};
  ppa_srm_rotation_angle_t ppa_rotation_{PPA_SRM_ROTATION_ANGLE_0};
  ppa_srm_color_mode_t ppa_out_color_mode_{PPA_SRM_COLOR_MODE_RGB565};
  // Size of the converted frame
  unsigned out_width_{0};
  unsigned out_height_{0};
#ifdef USE_XCSOAR_FRAME_BUFFERS
  mipi_dsi::MipiDsi *dsi_{nullptr};
  // The frame buffer the next frame is converted into; changed by loop() before it gives frame_consumed_
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
  StaticSemaphore_t frame_converted_buffer_{};
  SemaphoreHandle_t frame_converted_{nullptr};
  // The PPA operations of the frame still running, plus one while they are being started
  std::atomic<unsigned> conversions_left_{0};
  // Whether the frame being converted goes to the screen; false if starting a conversion failed
  std::atomic<bool> show_converted_{false};
  // Used by the XCSoar task only: a conversion was started and not waited for, and a frame was given to loop()
  // and not yet taken
  bool converting_{false};
  bool frame_given_{false};
  std::atomic<bool> frame_pending_{false};
  std::atomic<bool> task_stopped_{false};
  // Written before task_stopped_ is set
  bool task_quit_{false};
  LazyCallbackManager<void(bool)> stopped_callback_;
  // The latest alarm level from XCSoar's thread, for loop(); 0 for none
  std::atomic<uint8_t> pending_alarm_{0};
  LazyCallbackManager<void(uint8_t)> traffic_alarm_callback_;
  // The latest master volume in percent from XCSoar's thread, with VOLUME_PREVIEW for a preview, for loop();
  // NO_VOLUME for none
  static constexpr uint8_t VOLUME_PREVIEW = 0x80;
  static constexpr uint8_t NO_VOLUME = 0xFF;
  std::atomic<uint8_t> pending_volume_{NO_VOLUME};
  LazyCallbackManager<void(float, bool)> volume_callback_;
#ifdef USE_TOUCHSCREEN
  // The fingers XCSoar was last given, in the touchscreen's order
  static constexpr size_t MAX_FINGERS = 2;
  std::array<XCSoarEsp32::TouchPosition, MAX_FINGERS> fingers_{};
  size_t finger_count_{0};
#endif
  // A touchscreen passes touches to XCSoar, which then behaves as on a touch device
  bool touch_screen_{false};
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
#ifdef USE_XCSOAR_FLARM_RECEIVER
  flarm_receiver::FlarmReceiver *flarm_receiver_{nullptr};
#endif
#if defined(USE_XCSOAR_FLARM_RECEIVER) || defined(USE_XCSOAR_WIFI_NETWORKS)
  // The glider registration from club.json, set on XCSoar's thread and passed on in loop(): to the FLARM
  // receiver for its address, and to wifi_networks for the glider's own SkyEcho
  Mutex registration_lock_;
  char pending_registration_[16]{};
  std::atomic<bool> registration_pending_{false};
#endif
#ifdef USE_XCSOAR_SKYECHO
  skyecho::SkyEcho *skyecho_{nullptr};
#endif
#ifdef XCSOAR_STREAM_COUNT
  // The UART streams, then GDL90 and FLARM (if the SkyEcho has a FLARM UART)
  StaticVector<XCSoarEsp32::Stream, XCSOAR_STREAM_COUNT> streams_;
#endif
#ifdef USE_XCSOAR_WIFI_NETWORKS
  XCSoarEsp32::WifiHost wifi_host_{};
#endif
#ifdef USE_XCSOAR_BLE_SERIAL
  XCSoarEsp32::BleHost ble_host_{};
#endif
};

#ifdef USE_XCSOAR_UART
/// Passes what a UART receives to XCSoar's stream of the same name.
class XCSoarUartStream : public Component, public uart::UARTDevice {
 public:
  XCSoarUartStream(uart::UARTComponent *parent, const char *name) : uart::UARTDevice(parent), name_(name) {}
  void loop() override;

 protected:
  const char *name_;
};
#endif

}  // namespace esphome::xcsoar

#endif  // USE_ESP32_VARIANT_ESP32P4
