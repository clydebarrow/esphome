#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32

#include <cstdint>
#include <span>

#include <esp_gatt_common_api.h>
#include <esp_gattc_api.h>

#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble/ble_uuid.h"
#include "esphome/components/packet_interface/packet_interface.h"

namespace esphome::ble_client {

/**
 * A packet interface link over Bluetooth LE, as a GATT client of the device its ble_client connects to; that device
 * runs esp32_ble_server's packet interface platform, or anything with the same service.
 *
 * One characteristic carries packets both ways, a whole packet at a time: each packet sent is a write, without
 * response when the characteristic allows it, and each notification is a packet received. The link layer resends what
 * is lost while connected. Packets can be sent once notifications are on, and are refused before that and after a
 * disconnect.
 *
 * A packet must fit in one write, at most the negotiated ATT MTU less 3 bytes; ESP-IDF negotiates up to 517.
 */
class BLEClientPacketInterface : public packet_interface::PacketInterface, public BLEClientNode {
 public:
  /// @param tx_buffer Holds a packet being sent; its size is the largest packet, and it must outlive the link
  explicit BLEClientPacketInterface(std::span<uint8_t> tx_buffer)
      : packet_interface::PacketInterface(tx_buffer.size()), tx_buffer_(tx_buffer) {}

  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }
  bool send_to_interface(const packet_interface::PacketBuffer &data,
                         const packet_interface::PacketMetaData &meta_data) override;
  /// Nothing can be sent until the server's notifications are on
  size_t get_send_capacity() const override { return this->ready_ ? SIZE_MAX : 0; }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_uuids(const esp32_ble::ESPBTUUID &service, const esp32_ble::ESPBTUUID &characteristic) {
    this->service_uuid_ = service;
    this->characteristic_uuid_ = characteristic;
  }
  bool is_connected() const { return this->ready_; }

 protected:
  bool send_(const packet_interface::PacketBuffer &data);
  void reset_();

  esp32_ble::ESPBTUUID service_uuid_;
  esp32_ble::ESPBTUUID characteristic_uuid_;
  const std::span<uint8_t> tx_buffer_;
  uint16_t handle_{0};
  uint16_t cccd_handle_{0};
  uint16_t mtu_{ESP_GATT_DEF_BLE_MTU_SIZE};
  bool ready_{false};
  bool write_without_response_{false};
};

}  // namespace esphome::ble_client

#endif  // USE_ESP32
