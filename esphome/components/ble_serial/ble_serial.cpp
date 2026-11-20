#include "ble_serial.h"

#ifdef USE_ESP32

#include <algorithm>

#include "esphome/core/log.h"

namespace esphome::ble_serial {

ESPHOME_LOG_TAG(TAG, "ble_serial");

// A notification carries the ATT MTU less the opcode and handle
static constexpr uint16_t ATT_NOTIFY_OVERHEAD = 3;
// Notifications queued to the controller in one loop
static constexpr uint8_t NOTIFICATIONS_PER_LOOP = 4;

void BLESerial::setup() {
  for (auto *characteristic : this->characteristics_)
    characteristic->get_value().reserve(ESP_GATT_MAX_MTU_SIZE);
}

void BLESerial::dump_config() { ESP_LOGCONFIG(TAG, "BLE serial:\n  Services: %zu", this->characteristics_.size()); }

void BLESerial::gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                    esp_ble_gatts_cb_param_t *param) {
  switch (event) {
    case ESP_GATTS_CONNECT_EVT:
      if (this->connections_++ == 0)
        this->mtu_ = ESP_GATT_DEF_BLE_MTU_SIZE;
      ESP_LOGI(TAG, "Client connected");
      break;
    case ESP_GATTS_DISCONNECT_EVT:
      if (this->connections_ != 0 && --this->connections_ == 0) {
        this->mtu_ = ESP_GATT_DEF_BLE_MTU_SIZE;
        this->buffer_.clear();
        this->congested_ = false;
      }
      ESP_LOGI(TAG, "Client disconnected");
      break;
    case ESP_GATTS_MTU_EVT:
      this->mtu_ = this->connections_ <= 1 ? param->mtu.mtu : std::min(this->mtu_, param->mtu.mtu);
      ESP_LOGD(TAG, "MTU %u", this->mtu_);
      break;
    case ESP_GATTS_CONGEST_EVT:
      this->congested_ = param->congest.congested;
      break;
    default:
      break;
  }
}

void BLESerial::write(const std::string &text) {
  if (this->connections_ == 0)
    return;
  this->dropped_ += this->buffer_.write(text);
}

void BLESerial::loop() {
  if (this->dropped_ != 0) {
    ESP_LOGW(TAG, "Client is behind: %zu bytes dropped", this->dropped_);
    this->dropped_ = 0;
  }
  for (uint8_t i = 0; i != NOTIFICATIONS_PER_LOOP && !this->congested_ && !this->buffer_.empty(); i++) {
    const size_t size = this->buffer_.piece(this->mtu_ - ATT_NOTIFY_OVERHEAD);
    const char *data = this->buffer_.data();
    for (auto *characteristic : this->characteristics_) {
      characteristic->get_value().assign(data, data + size);
      characteristic->notify();
    }
    this->buffer_.consume(size);
  }
}

}  // namespace esphome::ble_serial

#endif  // USE_ESP32
