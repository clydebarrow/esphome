#pragma once

#include "esphome/core/component.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/uart/uart.h"
#include "flarm.h"
#include "gdl90.h"
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
  void ping_();
  void generate_simulated_ownship_();
  void generate_simulated_traffic_();
  void process_flarm_uart_();

  // Ownship tracking methods
  void set_ownship_position_(const gdl90PositionReport_t *position);
  void set_heartbeat_(const gdl90Heartbeat_t *heartbeat);
  bool is_gps_connected_();

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

  std::unique_ptr<socket::Socket> socket_{};
  std::unique_ptr<socket::Socket> ping_socket_{};

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
};

}  // namespace esphome::skyecho
