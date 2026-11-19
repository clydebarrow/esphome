#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32

#include <cstdint>

#include <esp_gatt_common_api.h>
#include <esp_gatts_api.h>

#include "esphome/components/esp32_ble_server/ble_2902.h"
#include "esphome/components/esp32_ble_server/ble_characteristic.h"
#include "esphome/components/esp32_ble_server/ble_server.h"
#include "esphome/components/packet_interface/packet_interface.h"

namespace esphome::esp32_ble_server {

/**
 * A packet interface link over Bluetooth LE, as the GATT server; ble_client's packet interface platform is the other
 * end.
 *
 * One characteristic carries packets both ways, a whole packet at a time: each write from a client is a packet
 * received, and each packet sent is a notification to the clients that have turned notifications on. The link layer
 * resends what is lost while connected, but a packet sent while no client is connected is refused.
 *
 * A packet must fit in one notification, at most the negotiated ATT MTU less 3 bytes; ESP-IDF negotiates up to 517.
 * With several clients connected, the smallest MTU sets the limit.
 */
class BLEServerPacketInterface : public packet_interface::PacketInterface {
 public:
  explicit BLEServerPacketInterface(size_t max_packet_size) : packet_interface::PacketInterface(max_packet_size) {}

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }
  bool send_to_interface(const packet_interface::PacketBuffer &data,
                         const packet_interface::PacketMetaData &meta_data) override;
  /// Nothing can be sent while no client is connected
  size_t get_send_capacity() const override { return this->connections_ == 0 ? 0 : SIZE_MAX; }

  void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

  void set_characteristic(BLECharacteristic *characteristic) { this->characteristic_ = characteristic; }
  bool is_connected() const { return this->connections_ != 0; }

 protected:
  bool send_(const packet_interface::PacketBuffer &data);

  BLECharacteristic *characteristic_{nullptr};
  // The smallest MTU of the connected clients; each starts at the minimum until it negotiates more
  uint16_t mtu_{ESP_GATT_DEF_BLE_MTU_SIZE};
  uint8_t connections_{0};
};

}  // namespace esphome::esp32_ble_server

#endif  // USE_ESP32
