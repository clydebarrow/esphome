#include "ble_server_packet_interface.h"

#ifdef USE_ESP32

#include <algorithm>

#include "esphome/core/log.h"

namespace esphome::esp32_ble_server {

using packet_interface::PacketBuffer;
using packet_interface::PacketMetaData;

ESPHOME_LOG_TAG(TAG, "esp32_ble_server.packet_interface");

// A notification carries the ATT MTU less the opcode and handle
static constexpr uint16_t ATT_NOTIFY_OVERHEAD = 3;

void BLEServerPacketInterface::setup() {
  // Each packet sent is copied into the value, so reserving now keeps the heap untouched after setup
  this->characteristic_->get_value().reserve(this->max_packet_size_);
  this->characteristic_->on_write([this](std::span<const uint8_t> data, uint16_t conn_id) {
    if (data.empty())
      return;
    if (data.size() > this->max_packet_size_) {
      ESP_LOGW(TAG, "Packet of %zu bytes is larger than %zu, discarding", data.size(), this->max_packet_size_);
      return;
    }
    this->on_receive_from_interface_(PacketBuffer(data.data(), data.size()), {});
  });
}

void BLEServerPacketInterface::dump_config() {
  ESP_LOGCONFIG(TAG, "BLE server packet interface:");
  packet_interface::PacketInterface::dump_config();
}

void BLEServerPacketInterface::gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                                   esp_ble_gatts_cb_param_t *param) {
  switch (event) {
    case ESP_GATTS_CONNECT_EVT:
      if (this->connections_++ == 0)
        this->mtu_ = ESP_GATT_DEF_BLE_MTU_SIZE;
      break;
    case ESP_GATTS_DISCONNECT_EVT:
      if (this->connections_ != 0 && --this->connections_ == 0)
        this->mtu_ = ESP_GATT_DEF_BLE_MTU_SIZE;
      break;
    case ESP_GATTS_MTU_EVT:
      // With one client the MTU is its own; with more, the smallest so every client can be sent to
      this->mtu_ = this->connections_ <= 1 ? param->mtu.mtu : std::min(this->mtu_, param->mtu.mtu);
      ESP_LOGD(TAG, "MTU %u", this->mtu_);
      break;
    default:
      break;
  }
}

// The packet is sent from the characteristic's value, which a log line further down could run an automation that
// sends into
bool BLEServerPacketInterface::send_to_interface(const PacketBuffer &data, const PacketMetaData &meta_data) {
  return this->send_exclusive_([&] { return this->send_(data); });
}

bool BLEServerPacketInterface::send_(const PacketBuffer &data) {
  size_t size = data.size();
  if (size == 0)
    return true;
  if (this->connections_ == 0)
    return false;
  if (size > this->max_packet_size_) {
    ESP_LOGW(TAG, "Packet of %zu bytes is larger than %zu", size, this->max_packet_size_);
    return false;
  }
  if (size > this->mtu_ - ATT_NOTIFY_OVERHEAD) {
    ESP_LOGW(TAG, "Packet of %zu bytes does not fit the MTU of %u", size, this->mtu_);
    return false;
  }
  auto &value = this->characteristic_->get_value();
  value.resize(size);
  data.copy_to(value.data(), size);
  this->characteristic_->notify();
  return true;
}

}  // namespace esphome::esp32_ble_server

#endif  // USE_ESP32
