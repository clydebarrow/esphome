#pragma once
#include "esphome/core/defines.h"
#ifdef USE_ESP_IDF

#include "esphome/core/component.h"

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/bytebuffer/bytebuffer.h"
#include "esphome/components/uart/uart.h"

#include "esphome/components/uart/uart_component_esp_idf.h"
#include <driver/rmt_tx.h>
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif

namespace esphome {
namespace sicom {

using namespace bytebuffer;

class SicomComponent;

enum DeviceState {
  UNSEEN,
  ENROLLING,
  ENROLLED,
  TALKING,
};

class SicomDevice : public PollingComponent, Parented<SicomComponent> {
 public:
  SicomDevice(uint8_t id, size_t voltage_cnt, size_t resistance_cnt, size_t current_cnt, size_t relay_cnt);

  void add_voltage_sensor(sensor::Sensor *sensor, size_t index) {
    if (index < this->voltage_sensors_.size()) {
      this->voltage_sensors_[index] = sensor;
    }
  }

  void add_resistance_sensor(sensor::Sensor *sensor, size_t index) {
    if (index < this->resistance_sensors_.size()) {
      this->resistance_sensors_[index] = sensor;
    }
  }
  void add_current_sensor(sensor::Sensor *sensor, size_t index) {
    if (index < this->current_sensors_.size()) {
      this->current_sensors_[index] = sensor;
    }
  }

  void update() override;
  virtual bool decode(ByteBuffer &data) = 0;

  float get_voltage(size_t index) const;
  float get_resistance(size_t index) const;
  float get_current(size_t index) const;
  bool get_relay(size_t index);

  size_t get_relay_count() { return relays_.size(); }
  size_t get_voltage_count() { return voltages_.size(); }
  size_t get_resistance_count() { return resistances_.size(); }
  size_t get_current_count() { return currents_.size(); }

  uint8_t get_id() { return id_; }
  void set_serial_number(uint32_t serial_number) { this->serial_number_ = serial_number; }
  uint32_t get_serial_number() { return this->serial_number_; }
  bool is_enrolled() { return this->state_ == ENROLLED; }
  void set_state(DeviceState state) { this->state_ = state; }
  DeviceState get_state() { return this->state_; }

 protected:
  uint32_t serial_number_{};
  uint8_t id_{};
  DeviceState state_{UNSEEN};
  std::vector<float> voltages_{};
  std::vector<float> resistances_{};
  std::vector<float> currents_{};
  std::vector<bool> relays_{};
  std::vector<sensor::Sensor *> voltage_sensors_{};
  std::vector<sensor::Sensor *> resistance_sensors_{};
  std::vector<sensor::Sensor *> current_sensors_{};
#ifdef USE_SWITCH
  std::vector<switch_::Switch *> relay_sensors_{};
#endif  // USE_SWITCH
};

class SicomSCQ25TDevice : public SicomDevice {
 public:
  SicomSCQ25TDevice() : SicomDevice(2, 3, 4, 4, 1){};
  bool decode(ByteBuffer &data) override;
};

class SicomST107Device : public SicomDevice {
 public:
  SicomST107Device() : SicomDevice(3, 3, 4, 0, 1){};
  bool decode(ByteBuffer &data) override;
};

class SicomSC301Device : public SicomDevice {
 public:
  SicomSC301Device() : SicomDevice(0xE, 2, 1, 1, 0){};
  bool decode(ByteBuffer &data) override;
};

class SicomSC303Device : public SicomDevice {
 public:
  SicomSC303Device() : SicomDevice(0x10, 2, 3, 1, 0){};
  bool decode(ByteBuffer &data) override;
};

class SicomComponent : public uart::UARTDevice, public PollingComponent {
 public:
  // void setup() override;
  void loop() override;
  void setup() override;
  void update() override;
  std::vector<uint8_t> read_msg(uint8_t address);
  void enrol_device_(uint32_t serial, size_t address);
  void process_data_(ByteBuffer &buffer);
  void process_byte_(uint8_t byte);
  void dump_config() override;

  void add_device(SicomDevice *device) { this->devices_.push_back(device); }
  void set_debug(bool debug) { this->debug_ = debug; }
  void set_tx_pin(uint8_t tx_pin) { this->tx_pin_ = tx_pin; }
  void set_tx_enable_pin(GPIOPin *tx_enable_pin) { this->tx_enable_pin_ = tx_enable_pin; }
  void set_uart_parent(uart::IDFUARTComponent *parent) { this->uart_ = parent; }
  [[noreturn]] void run_task();

 protected:
  void send_message_(uint8_t address, std::vector<uint8_t> data);
  void send_message_(uint8_t address, uint8_t cmd);
  void start_input_();
  bool input_started_{};
  bool debug_{};
  std::vector<SicomDevice *> devices_{};
  unsigned int current_device_{};
  uint8_t tx_pin_{};
  GPIOPin *tx_enable_pin_{};
  rmt_channel_handle_t channel_{};
  rmt_encoder_handle_t encoder_{};
  rmt_transmit_config_t transmit_config_{};
  uint32_t last_all_call_{};
  std::vector<uint8_t> rx_data_{};
  QueueHandle_t data_queue_{};
  uart::IDFUARTComponent *uart_{};
  QueueHandle_t rx_queue_{};
  uint8_t uart_num_{0xFF};
};

}  // namespace sicom
}  // namespace esphome

#endif
