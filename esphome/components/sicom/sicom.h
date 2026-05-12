#pragma once
#include "esphome/core/defines.h"
#ifdef USE_ESP32

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

namespace esphome::sicom {
using namespace bytebuffer;

enum DeviceState {
  DEVICE_UNSEEN,
  DEVICE_ENROLLING,
  DEVICE_ENROLLED,
};

enum DataType {
  SIGNED16 = 8,
  UNSIGNED16 = 9,
  NTC3950 = 10,
  SIGNED32 = 16,
  UNSIGNED32 = 17,
};
class SicomSensor {
 public:
  SicomSensor(sensor::Sensor *sensor) : sensor_(sensor) {}
  virtual bool decode(ByteBuffer &buffer) const = 0;
  void invalidate() { this->sensor_->publish_state(NAN); }

 protected:
  sensor::Sensor *sensor_;
};

template<DataType DATA_TYPE, size_t OFFSET, float SCALE> class SicomSensorImpl : public SicomSensor {
  static constexpr float LENGTH = DATA_TYPE / 4;

 public:
  SicomSensorImpl(sensor::Sensor *sensor) : SicomSensor(sensor) {}

  bool decode(ByteBuffer &buffer) const override {
    if (buffer.get_limit() < OFFSET + LENGTH) {
      return false;
    }
    float value = NAN;
    if constexpr (DATA_TYPE == SIGNED16) {
      value = buffer.get_int16(OFFSET) * SCALE;
    }
    if constexpr (DATA_TYPE == UNSIGNED16) {
      value = buffer.get_uint16(OFFSET) * SCALE;
    }
    if constexpr (DATA_TYPE == SIGNED32) {
      value = buffer.get_int32(OFFSET) * SCALE;
    }
    if constexpr (DATA_TYPE == UNSIGNED32) {
      value = buffer.get_uint32(OFFSET) * SCALE;
    }
    if constexpr (DATA_TYPE == NTC3950) {
      // Standard 3950 NTC thermistor: B=3950, R0=10kOhm at T0=25C.
      // T(K) = 1 / (1/T0 + (1/B) * ln(R/R0)); T(C) = T(K) - 273.15.
      float resistance = buffer.get_uint16(OFFSET) * SCALE;
      if (resistance > 0.0f) {
        constexpr float T0 = 298.15f;
        constexpr float R0 = 10000.0f;
        constexpr float BETA = 3950.0f;
        value = 1.0f / (1.0f / T0 + logf(resistance / R0) / BETA) - 273.15f;
      }
    }

    this->sensor_->publish_state(value);
    return true;
  }
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
  uint32_t next_loop_time_{};
  uint32_t next_allcall_time_{};
};

}  // namespace esphome::sicom

#endif
