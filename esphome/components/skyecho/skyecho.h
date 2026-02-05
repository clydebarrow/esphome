#pragma once

#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/components/network/util.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/uart/uart.h"
#include "flarm.h"
#include "gdl90.h"
#include <cmath>

namespace esphome {
namespace skyecho {

static const char *const TAG = "skyecho";
static const int PORT = 4000;
static int pingPorts[] = {63093, 47578};
static const char *pingPayload = "{\"App\": \"TraffiX\","
                                 "\"GDL90\": {\"port\": 4000}}";

class SkyEcho : public PollingComponent {
 public:
  void dump_config() override {
    if (this->flarm_uart_ != nullptr) {
      ESP_LOGCONFIG(TAG, "  FLARM UART: configured");
    }
  }
  void update() override {}
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_flarm_uart(uart::UARTComponent *uart) {
    this->flarm_uart_ = uart;
    // Set up FLARM parser callbacks
    this->flarm_parser_.set_pflau_callback([this](const FlarmPflau &data) { this->process_flarm_pflau_(data); });
    this->flarm_parser_.set_pflaa_callback([this](const FlarmPflaa &data) { this->process_flarm_pflaa_(data); });
    this->flarm_parser_.set_ownship_callback(
        [this](const OwnshipPosition &data) { this->process_flarm_ownship_(data); });
  }

  /**
   * Get current ownship position from FLARM GPS
   */
  const OwnshipPosition &get_flarm_ownship() const { return this->flarm_parser_.get_ownship(); }

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
    // Process GDL90 UDP data
    this->process_gdl90_();

    // Process FLARM UART data
    this->process_flarm_uart_();
  }

  void processPacket(gdl90Data_t *packet, struct in_addr *srcAddr) {
    switch (packet->id) {
      case GDL90_HEARTBEAT:
        esph_log_d(TAG, "Heartbeat @%d GPSvalid %s", (int) packet->heartbeat.timeStamp,
                   packet->heartbeat.gpsPosValid ? "true" : "false");
        // setHeartbeat(&packet->heartbeat);
        break;

      case GDL90_OWNSHIP_REPORT:
        esph_log_d(TAG, "Ownship report");
        // setOwnshipPosition(&packet->positionReport);
        break;
      case GDL90_TRAFFIC_REPORT:
        esph_log_d(TAG, "Traffic report");
        // lastTrafficMs = (uint32_t) (esp_timer_get_time() / 1000);
        // processTraffic(&packet->positionReport);
        break;

      case GDL90_OWNSHIP_GEO_ALT:
        esph_log_d(TAG, "Geo Alt");
        // snprintf(gdl90StatusText, sizeof(gdl90StatusText), "OwnGeoAlt @ %f", packet->ownshipAltitude.geoAltitude /
        // 0.3048f);
        break;

      case GDL90_FOREFLIGHT_ID:
        esph_log_d(TAG, "Forflight ID");
        //        if (!websocketOpening && strncmp(packet->foreflightId.name, "SkyEcho", 7) == 0)
        //          openWebsocket(srcAddr, 80);
        //        else if (!websocketOpening && strncmp(packet->foreflightId.name, "Sim:", 4) == 0) {
        //          int port = atoi(packet->foreflightId.name + 4);
        //          openWebsocket(srcAddr, port);
        //        }
        break;

      default:
        ESP_LOGI(TAG, "Received unhandled packet type %d", packet->id);
        break;
    }
    // if (gdl90Status != GDL90_STATE_RX) {
    // gdl90Status = GDL90_STATE_RX;
    // postMessage(EVENT_GDL90_CHANGE, NULL, 0);
    //}
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

  void process_gdl90_() {
    uint8_t buf[1024];
    if (!network::is_connected())
      return;
    sockaddr_in from_addr;
    socklen_t addr_len = sizeof from_addr;
    auto len = this->socket_->recvfrom(buf, sizeof buf, (sockaddr *) &from_addr, &addr_len);
    if (len >= 0) {
      esph_log_v(TAG, "Received %d bytes from GDL90", len);
      gdl90GetBlocks(
          buf, len,
          [this](const gdlDataPacket_t *packet, in_addr *srcAddr) -> void { this->block_callback(packet, srcAddr); },
          &from_addr.sin_addr);
    }
  }

  void process_flarm_uart_() {
    if (this->flarm_uart_ == nullptr)
      return;

    uint8_t buf[128];
    uint8_t idx = 0;
    while (this->flarm_uart_->available()) {
      this->flarm_uart_->read_byte(buf + idx);
      if (++idx == sizeof buf)
        break;
    }
    if (idx != 0) {
      esph_log_vv(TAG, "Processing %d bytes from FLARM UART", idx);
      this->flarm_parser_.process_bytes(buf, idx);
    }
  }

  void process_flarm_pflau_(const FlarmPflau &data) {
    esph_log_d(TAG, "FLARM PFLAU: rx=%d tx=%d gps=%d power=%d alarm=%d", data.rx, data.tx, data.gps, data.power,
               data.alarm_level);
    if (data.id_valid) {
      esph_log_d(TAG, "  Target ID: %06X, bearing=%d, vertical=%d, distance=%u", data.id, data.relative_bearing,
                 data.relative_vertical, data.relative_distance);
    }
    // TODO: Merge FLARM status with SkyEcho status
  }

  void process_flarm_pflaa_(const FlarmPflaa &data) {
    const OwnshipPosition &ownship = this->flarm_parser_.get_ownship();

    esph_log_d(TAG, "FLARM PFLAA: ID=%06X alarm=%d N=%d E=%d V=%d", data.id, data.alarm_level, data.relative_north,
               data.relative_east, data.relative_vertical);

    // Convert relative position to absolute if we have ownship position
    if (ownship.position_valid && data.east_valid) {
      // Approximate conversion: 1 degree latitude = 111,111 meters
      // 1 degree longitude = 111,111 * cos(latitude) meters
      float lat_offset = data.relative_north / 111111.0f;
      float lon_offset = data.relative_east / (111111.0f * cosf(ownship.latitude * M_PI / 180.0f));

      float target_lat = ownship.latitude + lat_offset;
      float target_lon = ownship.longitude + lon_offset;
      float target_alt = (ownship.altitude_msl_valid ? ownship.altitude_msl : 0) + data.relative_vertical;

      esph_log_d(TAG, "  Absolute position: lat=%.6f, lon=%.6f, alt=%.0f m", target_lat, target_lon, target_alt);

      if (data.track_valid) {
        esph_log_d(TAG, "  Track=%d, GS=%u, VS=%d, type=%d", data.track, data.ground_speed, data.climb_rate,
                   data.aircraft_type);
      }
      // TODO: Add to merged traffic list
    } else {
      esph_log_d(TAG, "  (No ownship position - relative only)");
    }
  }

  void process_flarm_ownship_(const OwnshipPosition &data) {
    esph_log_d(TAG, "FLARM Ownship: lat=%.6f, lon=%.6f, alt_msl=%.1f m, alt_baro=%.1f m", data.latitude, data.longitude,
               data.altitude_msl, data.altitude_baro);
    if (data.speed_valid) {
      esph_log_d(TAG, "  Speed=%.1f m/s, Track=%.1f deg", data.ground_speed, data.track);
    }
    // Store ownship position for traffic position calculation
    // The position is already stored in flarm_parser_.ownship_
  }

  std::unique_ptr<socket::Socket> socket_{};
  std::unique_ptr<socket::Socket> ping_socket_{};
  uart::UARTComponent *flarm_uart_{nullptr};
  FlarmParser flarm_parser_;
};

}  // namespace skyecho
}  // namespace esphome
