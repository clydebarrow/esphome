#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32

#include <cstdint>
#include <string>
#include <vector>

#include <esp_gatt_common_api.h>
#include <esp_gatts_api.h>

#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/esp32_ble_server/ble_characteristic.h"
#include "esphome/core/component.h"
#include "serial_buffer.h"

namespace esphome::ble_serial {

/**
 * A one-way serial stream to Bluetooth LE clients, as from a FLARM's data port to a flight app. It offers the two
 * serial services such apps look for, the HM-10's (FFE0, with FFE1 carrying the data) and Nordic UART (6E400001, with
 * TX on 6E400003), and sends the same text on each to the clients that have turned notifications on. What clients
 * write is ignored.
 *
 * Text is split into notifications of the ATT MTU less 3 bytes, a few each loop so the controller's queue is not
 * overrun; while it reports congestion, sending waits.
 */
class BLESerial : public Component {
 public:
  explicit BLESerial(size_t buffer_size) : buffer_(buffer_size) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

  void add_characteristic(esp32_ble_server::BLECharacteristic *characteristic) {
    this->characteristics_.push_back(characteristic);
  }
  /// Queues text to send; dropped while no client is connected.
  void write(const std::string &text);
  bool is_connected() const { return this->connections_ != 0; }

 protected:
  SerialBuffer buffer_;
  std::vector<esp32_ble_server::BLECharacteristic *> characteristics_;
  // The smallest MTU of the connected clients; each starts at the minimum until it negotiates more
  uint16_t mtu_{ESP_GATT_DEF_BLE_MTU_SIZE};
  uint8_t connections_{0};
  bool congested_{false};
  size_t dropped_{0};
};

}  // namespace esphome::ble_serial

#endif  // USE_ESP32
