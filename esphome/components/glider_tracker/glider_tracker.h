#pragma once

#include "esphome/core/defines.h"

#include <cstdint>

#include "esphome/components/gps/gps.h"
#include "esphome/components/packet_interface/packet_interface.h"
#include "esphome/core/helpers.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "flight_log.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_GLIDER_TRACKER_FLARM
#include "esphome/components/flarm_receiver/flarm_receiver.h"
#endif

namespace esphome::glider_tracker {

/**
 * A glider's tracker for the club: it broadcasts its position for the live map, every 5 s in flight and every 30 s on
 * the ground, and reports each flight to the clubhouse receiver, encrypted: its takeoff as it happens, and its summary
 * after landing, each sent every 10 s until acknowledged. Summaries are kept in flash until then.
 *
 * With a FLARM receiver on the same radio, the radio is given back to LoRa before each send, and held there a while
 * after a send to the clubhouse so its answer is heard. FLARM is listened for in flight, and on the ground while
 * nothing waits for the clubhouse's acknowledgement, or always if set.
 */
class GliderTracker : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_gps(gps::GPS *gps) { this->gps_ = gps; }
  void set_clubhouse(packet_interface::PacketInterface *port) { this->clubhouse_ = port; }
  void set_broadcast(packet_interface::PacketInterface *port) { this->broadcast_ = port; }
  void set_settings(const FlightSettings &settings) { this->log_ = FlightLog(settings); }
#ifdef USE_SENSOR
  void set_battery(sensor::Sensor *battery) { this->battery_ = battery; }
#endif
#ifdef USE_GLIDER_TRACKER_FLARM
  void set_flarm(flarm_receiver::FlarmReceiver *flarm) { this->flarm_ = flarm; }
#endif
  void set_flarm_on_ground(bool listen) { this->flarm_on_ground_ = listen; }
  /// False until the tracker has an address and key (tracker_identity): it then sends nothing
  void set_identified(bool identified) { this->identified_ = identified; }

  bool is_flying() const { return this->log_.is_flying(); }
  /// Seconds since takeoff, 0 on the ground or without a clock
  uint32_t flight_seconds() const;
  /// Summaries waiting for the clubhouse
  size_t queued() const { return this->log_.queued(); }
  /// The current fix and time, as the flight log takes them
  Sample sample() const;

  /// For bench tests: a fake flight of an hour ending now
  void fake_flight();
  /// For bench tests: a fake takeoff, or the landing of the flight in progress
  void fake_takeoff_or_landing();

  template<typename F> void add_on_takeoff_callback(F &&callback) {
    this->takeoff_callback_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_landing_callback(F &&callback) {
    this->landing_callback_.add(std::forward<F>(callback));
  }

 protected:
  void handle_event_(FlightLog::Event event);
  void save_();
  void send_position_();
  void send_takeoff_();
  void send_summary_();
  bool send_(packet_interface::PacketInterface *port, const uint8_t *data, size_t length, bool answered);
  bool identified_{true};
  void receive_(const packet_interface::PacketBuffer &data, const packet_interface::PacketMetaData &meta);

  gps::GPS *gps_{nullptr};
  packet_interface::PacketInterface *clubhouse_{nullptr};
  packet_interface::PacketInterface *broadcast_{nullptr};
#ifdef USE_SENSOR
  sensor::Sensor *battery_{nullptr};
#endif
#ifdef USE_GLIDER_TRACKER_FLARM
  flarm_receiver::FlarmReceiver *flarm_{nullptr};
#endif
  bool flarm_on_ground_{false};

  FlightLog log_;
  ESPPreferenceObject pref_;
  LazyCallbackManager<void()> takeoff_callback_;
  LazyCallbackManager<void()> landing_callback_;

  uint32_t last_update_ms_{0};
  uint32_t last_position_ms_{0};
  uint32_t last_takeoff_ms_{0};
  uint32_t last_summary_ms_{0};
  bool want_position_{true};
  bool want_takeoff_{false};
  bool want_summary_{true};
  uint32_t position_seq_{0};
  int16_t last_altitude_m_{0};
  uint32_t last_altitude_ms_{0};
};

}  // namespace esphome::glider_tracker
