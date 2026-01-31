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

class SkyEchoTextSensor;  // Forward declaration

static const char *const TAG = "skyecho";
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

  // Public accessor methods for text sensor
  bool getOwnshipPosition(ownship_t *position) {
    *position = this->ownship_;
    return this->isGpsConnected();
  }

  bool getHeartbeat(gdl90Heartbeat_t *heartbeat) {
    *heartbeat = this->heartbeat_;
    return this->isGpsConnected();
  }

  void getTraffic(traffic_t *buffer, size_t cnt) {
    this->sortTraffic();
    size_t copy_cnt = std::min(cnt, MAX_TRAFFIC_TRACKED);
    memcpy(buffer, this->traffic_, copy_cnt * sizeof(traffic_t));
  }

  void setup() override {
    this->socket_ = socket::socket_ip(SOCK_DGRAM, IPPROTO_UDP);
    this->ping_socket_ = socket::socket_ip(SOCK_DGRAM, IPPROTO_UDP);
    int enable = 1;
    int err = this->socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    if (err != 0) {
      ESP_LOGW(TAG, "Socket unable to set reuseaddr: errno %d", err);
      // we can still continue
    }
    err = this->socket_->setblocking(false);
    if (err != 0) {
      ESP_LOGW(TAG, "Socket unable to set nonblocking mode: errno %d", err);
      this->mark_failed();
      return;
    }

    struct sockaddr_storage server;

    socklen_t sl = socket::set_sockaddr_any((struct sockaddr *) &server, sizeof(server), PORT);
    if (sl == 0) {
      ESP_LOGW(TAG, "Socket unable to set sockaddr: errno %d", errno);
      this->mark_failed();
      return;
    }

    err = this->socket_->bind((struct sockaddr *) &server, sizeof(server));
    if (err != 0) {
      ESP_LOGW(TAG, "Socket unable to bind: errno %d", errno);
      this->mark_failed();
      return;
    }
    this->set_interval("ping", 10000, [this]() { this->ping_(); });
  }

  void loop() override {
    uint8_t buf[1024];
    if (!network::is_connected())
      return;
    sockaddr_in from_addr;
    socklen_t addr_len = sizeof from_addr;
    auto len = this->socket_->recvfrom(buf, sizeof buf, (sockaddr *) &from_addr, &addr_len);
    if (len >= 0) {
      esph_log_d(TAG, "Received %d bytes", len);
      gdl90GetBlocks(
          buf, len,
          [this](const gdlDataPacket_t *packet, in_addr *srcAddr) -> void { this->block_callback(packet, srcAddr); },
          &from_addr.sin_addr);
    }
  }

  void processPacket(gdl90Data_t *packet, struct in_addr *srcAddr) {
    switch (packet->id) {
      case GDL90_HEARTBEAT:
        esph_log_d(TAG, "Heartbeat @%d GPSvalid %s", (int) packet->heartbeat.timeStamp,
                   packet->heartbeat.gpsPosValid ? "true" : "false");
        this->setHeartbeat(&packet->heartbeat);
        break;

      case GDL90_OWNSHIP_REPORT:
        esph_log_d(TAG, "Ownship report");
        this->setOwnshipPosition(&packet->positionReport);
        break;

      case GDL90_TRAFFIC_REPORT:
        esph_log_d(TAG, "Traffic report");
        this->processTraffic(&packet->positionReport);
        break;

      case GDL90_OWNSHIP_GEO_ALT:
        esph_log_d(TAG, "Geo Alt: %.1f m", packet->ownshipAltitude.geoAltitude);
        this->ownship_altitude_ = packet->ownshipAltitude;
        break;

      case GDL90_FOREFLIGHT_ID:
        esph_log_d(TAG, "Foreflight ID: %s", packet->foreflightId.name);
        break;

      default:
        ESP_LOGI(TAG, "Received unhandled packet type %d", packet->id);
        break;
    }
  }

  void block_callback(const gdlDataPacket_t *packet, in_addr *srcAddr) {
    gdl90Data_t data;
    if (packet->err != GDL90_ERR_NONE) {
      ESP_LOGI(TAG, "DecodeBlock failed - %s: id %d, len %d", gdl90ErrorMessage(packet->err), packet->data[0],
               packet->len);
    } else {
      memset(&data, 0, sizeof(data));
      gdl90Err_t err = gdl90DecodeBlock(packet->data, packet->len, &data);
      if (err == GDL90_ERR_NONE) {
        processPacket(&data, srcAddr);
      } else
        ESP_LOGI(TAG, "Decode packet id %d failed with err %s", data.id, gdl90ErrorMessage(err));
    }
  }

 protected:
  void ping_() {
    if (!network::is_connected())
      return;
    for (size_t i = 0; i != LWIP_ARRAYSIZE(pingPorts); i++) {
      struct sockaddr addr;
      socket::set_sockaddr(&addr, sizeof(addr), {"255.255.255.255"}, pingPorts[i]);
      auto err = this->ping_socket_->sendto(pingPayload, strlen(pingPayload), 0, &addr, sizeof(addr));
      if (err < 0) {
        esph_log_e(TAG, "Sendto failed on port %d with err %d", pingPorts[i], err);
      }
    }
  }

  // Ownship tracking methods
  void setOwnshipPosition(const gdl90PositionReport_t *position) {
    this->ownship_.report = *position;
    this->ownship_.timestampMs = millis();
    esph_log_i(TAG, "Ownship: lat=%.4f lon=%.4f alt=%.0fm spd=%.0fm/s", position->latitude, position->longitude,
               position->altitude, position->groundSpeed);
  }

  void setHeartbeat(const gdl90Heartbeat_t *heartbeat) { this->heartbeat_ = *heartbeat; }

  bool isGpsConnected() {
    return this->heartbeat_.gpsPosValid && this->ownship_.timestampMs > 0 &&
           (millis() - this->ownship_.timestampMs) < MAX_POSITION_AGE_MS;
  }

  // Traffic tracking helper methods
  static int compareTraffic(const void *tp1, const void *tp2) {
    const traffic_t *t1 = (const traffic_t *) tp1;
    const traffic_t *t2 = (const traffic_t *) tp2;

    if (!t1->active && !t2->active)
      return 0;
    if (!t2->active)
      return -1;
    if (!t1->active)
      return 1;
    return (int) ((t1->distance - t2->distance) * 1000.0f);
  }

  void sortTraffic() {
    uint32_t now = millis();
    uint32_t oldMs = now - MAX_TRAFFIC_AGE_MS;

    // Mark old traffic as inactive
    for (size_t i = 0; i < MAX_TRAFFIC_TRACKED; i++) {
      if (this->traffic_[i].active && this->traffic_[i].timestampMs < oldMs) {
        this->traffic_[i].active = false;
      }
    }

    // Sort by active status and distance
    qsort(this->traffic_, MAX_TRAFFIC_TRACKED, sizeof(traffic_t), compareTraffic);
  }

  traffic_t *getEntry() {
    this->sortTraffic();
    // The last one is the furthest away, or unused
    return &this->traffic_[MAX_TRAFFIC_TRACKED - 1];
  }

  void updateTraffic(traffic_t *tp, const gdl90PositionReport_t *report, float distance) {
    tp->timestampMs = millis();
    tp->report = *report;
    tp->distance = distance;
    tp->active = true;
    esph_log_i(TAG, "Traffic: %s dist=%.0fm alt=%.0fm spd=%.0fm/s", report->callsign, distance, report->altitude,
               report->groundSpeed);
  }

  void processTraffic(const gdl90PositionReport_t *report) {
    if (!this->isGpsConnected())
      return;

    float distance = trafficDistance(&this->ownship_.report, report);

    // Check if target is already in our list
    for (size_t i = 0; i < MAX_TRAFFIC_TRACKED; i++) {
      if (this->traffic_[i].report.addressType == report->addressType &&
          this->traffic_[i].report.address == report->address) {
        this->updateTraffic(&this->traffic_[i], report, distance);
        return;
      }
    }

    // Get a slot for new traffic
    traffic_t *tp = this->getEntry();

    // Use the slot if it's outdated or further away than the new target
    if (!tp->active || tp->distance > distance) {
      this->updateTraffic(tp, report, distance);
    }
  }

  size_t getActiveTrafficCount() {
    size_t count = 0;
    uint32_t now = millis();
    uint32_t oldMs = now - MAX_TRAFFIC_AGE_MS;

    for (size_t i = 0; i < MAX_TRAFFIC_TRACKED; i++) {
      if (this->traffic_[i].active && this->traffic_[i].timestampMs >= oldMs) {
        count++;
      }
    }
    return count;
  }

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
};

}  // namespace skyecho
}  // namespace esphome
