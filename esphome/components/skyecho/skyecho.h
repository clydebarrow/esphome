#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/uart/uart.h"
#include "flarm.h"
#include "gdl90.h"
#include "threat.h"
#include "traffic_manager.h"
#include <algorithm>

namespace esphome::skyecho {

class SkyEchoTextSensor;         // Forward declaration
class SkyEchoTrafficListSensor;  // Forward declaration
class SkyEchoSimulateSwitch;     // Forward declaration

static constexpr int PORT = 4000;
static constexpr int PINGPORTS[] = {63093, 47578};
static const char *const PINGPAYLOAD = "{\"App\": \"TraffiX\","
                                       "\"GDL90\": {\"port\": 4000}}";

// Traffic tracking constants
static const size_t MAX_TRAFFIC_TRACKED = 20;      // maximum number of aircraft to track
static const uint32_t MAX_TRAFFIC_AGE_MS = 10000;  // max traffic age in ms
static const uint32_t MAX_POSITION_AGE_MS = 5000;  // max position age in ms

// Ownship data structure
struct OwnshipT {
  gdl90PositionReport_t report;
  uint32_t timestampMs;  // when report last updated in ms
};

// Traffic data structure
struct TrafficT {
  gdl90PositionReport_t report;
  float distance;        // calculated distance from us in meters
  uint32_t timestampMs;  // when report last updated in ms
  bool active;
};

class SkyEcho : public PollingComponent {
 public:
  void dump_config() override;
  void update() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void register_listener(PollingComponent *sensor) { this->listeners_.push_back(sensor); }
  void set_simulate(bool simulate) { this->simulate_ = simulate; }

  // FLARM UART configuration
  void set_flarm_uart(uart::UARTComponent *uart);

  /**
   * Get current ownship position from FLARM GPS
   */
  const OwnshipPosition &get_flarm_ownship() const { return this->flarm_parser_.get_ownship(); }

  /**
   * Get the traffic manager for access to merged traffic list
   */
  const TrafficManager &get_traffic_manager() const { return this->traffic_manager_; }

  /**
   * Called with each GDL90 datagram as received, before it is parsed.
   */
  template<typename F> void add_on_gdl90_data_callback(F &&callback) {
    this->gdl90_data_callback_.add(std::forward<F>(callback));
  }

  /**
   * Called with the bytes read from the FLARM UART, before they are parsed.
   */
  template<typename F> void add_on_flarm_data_callback(F &&callback) {
    this->flarm_data_callback_.add(std::forward<F>(callback));
  }

  bool has_flarm_uart() const { return this->flarm_uart_ != nullptr; }

  // Collision prediction: close proximity, how long before reaching it the alarm is raised, and which targets are
  // ignored. Distances in meters, times in seconds.
  void set_alarm_distance(float meters) { this->threat_settings_.alarm_distance = meters; }
  void set_alarm_altitude(float meters) { this->threat_settings_.alarm_altitude = meters; }
  void set_glider_alarm_altitude(float meters) { this->threat_settings_.glider_alarm_altitude = meters; }
  void set_alarm_time(float seconds) { this->threat_settings_.alarm_time = seconds; }
  void set_max_distance(float meters) { this->threat_settings_.max_distance = meters; }
  void set_max_altitude_difference(float meters) { this->threat_settings_.max_altitude_difference = meters; }
  const ThreatSettings &get_threat_settings() const { return this->threat_settings_; }
  /// Seconds before close proximity at which to warn about a threat, longest first.
  void set_warning_times(std::initializer_list<uint16_t> seconds) { this->warnings_.set_times(seconds); }

  /// The most significant target at the latest assessment, made at each update.
  const Threat &get_threat() const { return this->threat_; }

  /// Called after each assessment with the most significant target.
  template<typename F> void add_on_threat_callback(F &&callback) {
    this->threat_callback_.add(std::forward<F>(callback));
  }

  /// Called once at each warning time with that time in seconds, while a threat raises the alarm. A threat first
  /// seen after some warning times have passed gives one call, for the shortest of those.
  template<typename F> void add_on_threat_warning_callback(F &&callback) {
    this->warning_callback_.add(std::forward<F>(callback));
  }

  /// The IPv4 address the GDL90 data last came from, in network byte order; 0 before any has arrived.
  uint32_t get_source_address() const { return this->source_address_; }

  /**
   * Set callback for traffic updates
   */
  void set_traffic_callback(TrafficCallback callback) { this->traffic_manager_.set_callback(std::move(callback)); }

  // Public accessor methods for text sensor
  bool get_ownship_position(OwnshipT *position);
  bool get_heartbeat(gdl90Heartbeat_t *heartbeat);
  void get_traffic(TrafficT *buffer, size_t cnt);

  void setup() override;
  void loop() override;

  void process_packet(gdl90Data_t *packet, struct in_addr *src_addr);
  void block_callback(const gdlDataPacket_t *packet, in_addr *src_addr);

 protected:
  void ping_() const;
  void generate_simulated_ownship_();
  void generate_simulated_traffic_();
  void process_flarm_uart_();

  // Ownship tracking methods
  void set_ownship_position_(const gdl90PositionReport_t *position);
  void set_heartbeat_(const gdl90Heartbeat_t *heartbeat);
  bool is_gps_connected_() const;

  // Traffic tracking helper methods
  static int compare_traffic(const void *tp1, const void *tp2);
  void sort_traffic_();
  TrafficT *get_entry_();
  void update_traffic_(TrafficT *tp, const gdl90PositionReport_t *report, float distance);
  void process_traffic_(const gdl90PositionReport_t *report);
  size_t get_active_traffic_count_();

  // FLARM processing methods
  void process_flarm_pflau_(const FlarmPflau &data);
  void process_flarm_pflaa_(const FlarmPflaa &data);
  void process_flarm_ownship_(const OwnshipPosition &data);
  void update_ownship_from_gdl90_(const gdl90PositionReport_t &report);
  // Dead-reckon the traffic to now, pick the most significant target and give the warnings that are due.
  void assess_threats_();

  std::unique_ptr<socket::Socket> socket_{};
  std::unique_ptr<socket::Socket> ping_socket_{};
  uint32_t source_address_{0};

  // Ownship data
  gdl90Heartbeat_t heartbeat_{};
  OwnshipT ownship_{};
  gdl90OwnshipAltitude_t ownship_altitude_{};

  // Traffic data
  TrafficT traffic_[MAX_TRAFFIC_TRACKED]{};

  // Text sensors
  std::vector<PollingComponent *> listeners_{};

  // Configuration
  bool simulate_{false};

  // FLARM support
  uart::UARTComponent *flarm_uart_{nullptr};
  FlarmParser flarm_parser_{};
  TrafficManager traffic_manager_{};

  // Collision prediction
  ThreatSettings threat_settings_{};
  ThreatWarnings warnings_{};
  Threat threat_{};
  LazyCallbackManager<void(const Threat &)> threat_callback_;
  LazyCallbackManager<void(uint32_t)> warning_callback_;

  LazyCallbackManager<void(const uint8_t *, size_t)> gdl90_data_callback_;
  LazyCallbackManager<void(const uint8_t *, size_t)> flarm_data_callback_;
};

}  // namespace esphome::skyecho
