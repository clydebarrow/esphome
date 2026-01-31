#pragma once

#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/components/network/util.h"
#include "esphome/components/socket/socket.h"
#include "gdl90.h"

namespace esphome {
namespace skyecho {

static const char *const TAG = "skyecho";
static const int PORT = 4000;
static int pingPorts[] = {63093, 47578};
static const char *pingPayload = "{\"App\": \"TraffiX\","
                                 "\"GDL90\": {\"port\": 4000}}";

class SkyEcho : public PollingComponent {
 public:
  void dump_config() override {}
  void update() override {}
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

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
  std::unique_ptr<socket::Socket> socket_{};
  std::unique_ptr<socket::Socket> ping_socket_{};
};

}  // namespace skyecho
}  // namespace esphome
