#pragma once
#include "esphome/core/defines.h"
#ifdef USE_ESP_IDF

#include "esphome/core/component.h"

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
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

enum DeviceState {
  DEVICE_UNSEEN,
  DEVICE_ENROLLING,
  DEVICE_ENROLLED,
};

enum DataType {
  SIGNED16 = 8,
  UNSIGNED16 = 9,
  SIGNED32 = 16,
  UNSIGNED32 = 17,
};
class SicomSensor {
 public:
  SicomSensor(sensor::Sensor *sensor, size_t offset, DataType data_type, float scale)
      : sensor_(sensor), offset_(offset), data_type_(data_type), scale_(scale) {
    this->length_ = (size_t) data_type / 4;
  }

  bool decode(ByteBuffer &buffer) const;
  void invalidate() { this->sensor_->publish_state(NAN); }

 protected:
  sensor::Sensor *sensor_;
  size_t offset_;
  DataType data_type_;
  float scale_;
  size_t length_;
};

class SicomDevice {
 public:
  SicomDevice(uint8_t id) : id_(id) {}

  void add_sensor(SicomSensor *sensor) { this->sensors_.push_back(sensor); }
  void set_status_sensor(binary_sensor::BinarySensor *status_sensor) { this->status_sensor_ = status_sensor; }

  void invalidate();

  void decode(const std::vector<uint8_t> &data);

  uint8_t get_id() const { return id_; }
  void set_serial_number(uint32_t serial_number) { this->serial_number_ = serial_number; }
  uint32_t get_serial_number() const { return this->serial_number_; }
  bool is_enrolled() const { return this->state_ == DEVICE_ENROLLED; }
  bool is_enrolling() const { return this->state_ == DEVICE_ENROLLING; }
  void set_state(DeviceState state);
  DeviceState get_state() const { return this->state_; }
  void set_address(uint8_t address) { this->address_ = address; }
  uint8_t get_address() const { return this->address_; }

 protected:
  uint32_t serial_number_{};
  uint8_t id_;
  uint8_t address_{};
  DeviceState state_{DEVICE_UNSEEN};
  std::vector<SicomSensor *> sensors_{};
  binary_sensor::BinarySensor *status_sensor_{nullptr};
  uint32_t last_data_time_{};
};

class SicomComponent : public uart::UARTDevice, public Component {
 public:
  // void setup() override;
  void setup() override;
  bool try_read_(uint8_t *data);
  void loop() override;
  void send_enrol_message_(const SicomDevice *device) const;
  bool enrol_device_(const std::vector<uint8_t> &data);
  bool process_message_(const std::vector<uint8_t> &buffer);
  void dump_config() override;

  void add_device(SicomDevice *device) {
    this->devices_.push_back(device);
    device->set_address(this->devices_.size());
  }
  void set_debug(bool debug) { this->debug_ = debug; }
  void set_tx_pin(uint8_t tx_pin) { this->tx_pin_ = tx_pin; }
  void set_tx_enable_pin(GPIOPin *tx_enable_pin) { this->tx_enable_pin_ = tx_enable_pin; }

 protected:
  std::vector<uint8_t> read_message_();
  void send_message_(uint8_t address, std::vector<uint8_t> data) const;
  void send_message_(uint8_t address, uint8_t cmd) const;
  bool confirm_enrolment_(const std::vector<uint8_t> &data) const;
  void send_poll_(const SicomDevice *device) const;
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
  uint16_t next_device_{};
};

}  // namespace sicom
}  // namespace esphome

#endif
