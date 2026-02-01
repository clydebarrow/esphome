#pragma once

#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/network/util.h"
#include "esphome/components/socket/socket.h"
#include "gdl90.h"
#include <algorithm>

namespace esphome {
namespace skyecho {

class SkyEchoTextSensor;         // Forward declaration
class SkyEchoTrafficListSensor;  // Forward declaration
class SkyEchoSimulateSwitch;     // Forward declaration

static const int PORT = 4000;
static int pingPorts[] = {63093, 47578};
static const char *pingPayload = "{\"App\": \"TraffiX\","
                                 "\"GDL90\": {\"port\": 4000}}";

// Traffic tracking constants
static const size_t MAX_TRAFFIC_TRACKED = 20;      // maximum number of aircraft to track
static const uint32_t MAX_TRAFFIC_AGE_MS = 10000;  // max traffic age in ms
static const uint32_t MAX_POSITION_AGE_MS = 5000;  // max position age in ms

// Ownship data structure
struct ownship_t {
  gdl90PositionReport_t report;
  uint32_t timestampMs;  // when report last updated in ms
};

// Traffic data structure
struct traffic_t {
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

  void register_text_sensor(class SkyEchoTextSensor *sensor) { this->text_sensors_.push_back(sensor); }
  void register_traffic_list_sensor(class SkyEchoTrafficListSensor *sensor) {
    this->traffic_list_sensors_.push_back(sensor);
  }
  void set_simulate_switch(class SkyEchoSimulateSwitch *switch_component) { this->simulate_switch_ = switch_component; }
  void set_simulate(bool simulate) { this->simulate_ = simulate; }

  // Public accessor methods for text sensor
  bool getOwnshipPosition(ownship_t *position);
  bool getHeartbeat(gdl90Heartbeat_t *heartbeat);
  void getTraffic(traffic_t *buffer, size_t cnt);

  void setup() override;
  void loop() override;

  void processPacket(gdl90Data_t *packet, struct in_addr *srcAddr);
  void block_callback(const gdlDataPacket_t *packet, in_addr *srcAddr);

 protected:
  void ping_();
  void generate_simulated_ownship();
  void generate_simulated_traffic();

  // Ownship tracking methods
  void setOwnshipPosition(const gdl90PositionReport_t *position);
  void setHeartbeat(const gdl90Heartbeat_t *heartbeat);
  bool isGpsConnected();

  // Traffic tracking helper methods
  static int compareTraffic(const void *tp1, const void *tp2);
  void sortTraffic();
  traffic_t *getEntry();
  void updateTraffic(traffic_t *tp, const gdl90PositionReport_t *report, float distance);
  void processTraffic(const gdl90PositionReport_t *report);
  size_t getActiveTrafficCount();

 protected:
  std::unique_ptr<socket::Socket> socket_{};
  std::unique_ptr<socket::Socket> ping_socket_{};

  // Ownship data
  gdl90Heartbeat_t heartbeat_{};
  ownship_t ownship_{};
  gdl90OwnshipAltitude_t ownship_altitude_{};

  // Traffic data
  traffic_t traffic_[MAX_TRAFFIC_TRACKED]{};

  // Text sensors
  std::vector<class SkyEchoTextSensor *> text_sensors_{};
  std::vector<class SkyEchoTrafficListSensor *> traffic_list_sensors_{};

  // Simulate switch
  class SkyEchoSimulateSwitch *simulate_switch_{nullptr};

  // Configuration
  bool simulate_{false};
};

}  // namespace skyecho
}  // namespace esphome
