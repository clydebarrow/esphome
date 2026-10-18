#pragma once

#include "../espnow_component.h"

#if defined(USE_ESP32) || defined(USE_ESP8266)

#include <memory>

#include "esphome/components/packet_interface/packet_interface.h"

namespace esphome::espnow {

/**
 * A packet interface link over ESP-NOW.
 *
 * With a peer address other than broadcast, packets go to that device only, and only packets from it are received,
 * so the link provides the addressing capability itself. With the broadcast address, packets go to every device on
 * the channel and every packet is received; an addressing layer then tells the devices apart.
 *
 * ESP-NOW queues each packet and reports later whether it was delivered; a reliability layer above resends what is
 * lost.
 */
class ESPNowPacketInterface : public packet_interface::PacketInterface,
                              public ESPNowReceivedPacketHandler,
                              public ESPNowBroadcastHandler {
 public:
  ESPNowPacketInterface(ESPNowComponent *parent, peer_address_t peer_address);

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  bool send_to_interface(const packet_interface::PacketBuffer &data,
                         const packet_interface::PacketMetaData &meta_data) override;

  bool on_receive(const ESPNowRecvInfo &info, const uint8_t *data, uint16_t size) override;
  bool on_broadcast(const ESPNowRecvInfo &info, const uint8_t *data, uint16_t size) override;

 protected:
  bool is_broadcast_() const {
    return memcmp(this->peer_address_.data(), ESPNOW_BROADCAST_ADDR, ESP_NOW_ETH_ALEN) == 0;
  }
  void receive_(const ESPNowRecvInfo &info, const uint8_t *data, uint16_t size);

  ESPNowComponent *parent_;
  peer_address_t peer_address_;
  // For packets split across buffers, allocated in setup()
  std::unique_ptr<uint8_t[]> tx_buffer_;
};

}  // namespace esphome::espnow

#endif  // USE_ESP32 || USE_ESP8266
