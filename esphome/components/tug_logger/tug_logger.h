#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"

#include "flight_detector.h"
#include "tow_store.h"

#ifdef USE_TUG_LOGGER_SKYECHO
#include "esphome/components/skyecho/skyecho.h"
#endif
#ifdef USE_TUG_LOGGER_GPS
#include "esphome/components/gps/gps.h"
#endif
#ifdef USE_TUG_LOGGER_WIFI_NETWORKS
#include "esphome/components/wifi_networks/wifi_networks.h"
#endif

namespace esphome::tug_logger {

enum class GpsSource : uint8_t {
  GPS_SOURCE_NONE,
  GPS_SOURCE_SKYECHO,
  GPS_SOURCE_NMEA,
};

/// Detects flights from a SkyEcho or an NMEA GPS, keeps tows on the SD card for the pilot to review, and uploads the
/// confirmed ones. A tow is uploaded only while the network in use is not a SkyEcho, which has no internet.
class TugLogger : public Component
#ifdef USE_TUG_LOGGER_GPS
    ,
                  public gps::GPSListener
#endif
{
 public:
  explicit TugLogger(const char *card_path) : store_(card_path) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_safe_shutdown() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_time(time::RealTimeClock *time) { this->time_ = time; }
  void set_external_power(binary_sensor::BinarySensor *sensor) { this->external_power_ = sensor; }
  void set_battery(sensor::Sensor *sensor) { this->battery_ = sensor; }
#ifdef USE_TUG_LOGGER_SKYECHO
  void set_skyecho(skyecho::SkyEcho *skyecho) { this->skyecho_ = skyecho; }
#endif
#ifdef USE_TUG_LOGGER_WIFI_NETWORKS
  void set_wifi_networks(wifi_networks::WifiNetworks *networks) { this->wifi_networks_ = networks; }
  wifi_networks::WifiNetworks *get_wifi_networks() { return this->wifi_networks_; }
#endif
#ifdef USE_TUG_LOGGER_GPS
  void on_update(TinyGPSPlus &tiny_gps) override;
#endif
  void set_takeoff_speed(float speed) { this->settings_.takeoff_speed_mps = speed; }
  void set_takeoff_confirm(uint32_t ms) { this->settings_.takeoff_confirm_ms = ms; }
  void set_landing_speed(float speed) { this->settings_.landing_speed_mps = speed; }
  void set_landing_confirm(uint32_t ms) { this->settings_.landing_confirm_ms = ms; }
  void set_shutdown_window(uint32_t ms) { this->shutdown_window_ms_ = ms; }
  void set_upload_url(const char *url) { this->upload_url_ = url; }
  void set_upload_key(const char *key) { this->upload_key_ = key; }

  template<typename F> void add_on_takeoff_callback(F &&callback) {
    this->takeoff_callback_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_landed_callback(F &&callback) {
    this->landed_callback_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_power_off_callback(F &&callback) {
    this->power_off_callback_.add(std::forward<F>(callback));
  }

  // For the user interface; all on the main loop
  TowStore &store() { return this->store_; }
  bool is_flying() const { return this->detector_.is_flying(); }
  int32_t current_height_ft() const { return this->detector_.current_height_ft(); }
  uint32_t flight_elapsed_s() const { return this->detector_.flight().elapsed_s; }
  bool flight_data_lost() const { return this->is_flying() && this->source_ == GpsSource::GPS_SOURCE_NONE; }
  const TowDetails &flight_details() const { return this->flight_details_; }
  TowDetails &next_details() { return this->next_details_; }
  /// Keeps what the pilot entered for the next tow; a restart keeps the tug pilot too.
  void next_details_changed();
  const Provisioning &provisioning() const { return this->provisioning_; }
  bool has_provisioning() const { return this->provisioned_; }
  bool card_ok() const { return this->card_ok_; }
  GpsSource gps_source() const { return this->source_; }
  bool clock_valid() { return this->time_ != nullptr && this->time_->now().is_valid(); }
  std::string clock_text();
  size_t queued_count() const { return this->queued_.load(); }
  size_t failed_count() const { return this->failed_.load(); }
  size_t review_count();
  float battery_voltage() const { return this->battery_ != nullptr ? this->battery_->state : NAN; }
  bool on_skyecho_network() const { return this->on_skyecho_network_.load(); }
  /// Confirms a tow and updates the queue count.
  ConfirmResult confirm(uint32_t id);

  /// Stays up while the screen is in use on battery.
  void note_activity() { this->last_activity_ms_ = millis(); }

  /// Run by the upload task.
  void upload_step();

 protected:
  void tick_();
  bool start_store_();
  void close_saved_flight_();
  PositionSample read_position_();
  void on_takeoff_();
  void on_landed_();
  void save_flight_();
  void check_tow_watch_();
  void check_network_();
  void check_power_();
  time_t epoch_();

  time::RealTimeClock *time_{nullptr};
  binary_sensor::BinarySensor *external_power_{nullptr};
  sensor::Sensor *battery_{nullptr};
#ifdef USE_TUG_LOGGER_SKYECHO
  skyecho::SkyEcho *skyecho_{nullptr};
#endif
#ifdef USE_TUG_LOGGER_WIFI_NETWORKS
  wifi_networks::WifiNetworks *wifi_networks_{nullptr};
#endif
#ifdef USE_TUG_LOGGER_GPS
  uint32_t nmea_ms_{0};
  float nmea_speed_mps_{0.0f};
  float nmea_altitude_m_{0.0f};
  bool nmea_speed_valid_{false};
  bool nmea_altitude_valid_{false};
#endif

  DetectorSettings settings_{};
  FlightDetector detector_{};
  TowStore store_;
  Provisioning provisioning_{};
  TowDetails next_details_{};
  TowDetails flight_details_{};
  GpsSource source_{GpsSource::GPS_SOURCE_NONE};

  // A flight found on the card at start: carried on if the aircraft is still moving, otherwise closed
  FlightSnapshot saved_flight_{};
  TowDetails saved_details_{};
  time_t saved_at_{0};
  bool has_saved_flight_{false};

  const char *upload_url_{nullptr};
  const char *upload_key_{nullptr};
  uint32_t last_position_log_ms_{0};
  uint32_t last_tick_ms_{0};
  uint32_t last_store_try_ms_{0};
  uint32_t last_save_ms_{0};
  uint32_t last_card_check_ms_{0};
  uint32_t last_network_check_ms_{0};
  uint32_t last_activity_ms_{0};
  uint32_t power_lost_ms_{0};
  uint32_t shutdown_window_ms_{600000};
  bool store_ready_{false};
  bool provisioned_{false};
  bool card_ok_{false};
  bool had_external_power_{true};
  bool powering_off_{false};

  std::atomic<size_t> queued_{0};
  std::atomic<size_t> failed_{0};
  std::atomic<bool> on_skyecho_network_{false};
  uint32_t last_upload_ms_{0};

  LazyCallbackManager<void()> takeoff_callback_;
  LazyCallbackManager<void()> landed_callback_;
  LazyCallbackManager<void()> power_off_callback_;
};

}  // namespace esphome::tug_logger
