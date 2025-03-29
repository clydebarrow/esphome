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

const size_t MAX_DEVICES = 30; // maximum number of messages in the queue
class SicomComponent;

enum DeviceState {
  UNSEEN,
  ENROLLING,
  ENROLLED,
  TALKING,
};

enum State {
  STATE_ALL_CALL = 0,
  STATE_CALLING,
  STATE_ENROLLING,
  STATE_ENROLLING_2,
  STATE_POLLING,
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
    this->length_ = (size_t)data_type / 4;
  }

  bool decode(ByteBuffer &buffer);
  void invalidate() { this->sensor_->publish_state(NAN); }

 protected:
  sensor::Sensor *sensor_;
  size_t offset_;
  DataType data_type_;
  float scale_;
  size_t length_;
};

class SicomDevice : public PollingComponent, Parented<SicomComponent> {
 public:
  SicomDevice(uint8_t id) : id_(id) {}

  void add_sensor(SicomSensor *sensor) { this->sensors_.push_back(sensor); }

  void invalidate();

  void update() override;
  bool decode(ByteBuffer &data);

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
  std::vector<SicomSensor *> sensors_{};
};

class SicomComponent : public uart::UARTDevice, public PollingComponent {
 public:
  // void setup() override;
  void loop() override;
  void setup() override;
  void update() override;
  std::vector<uint8_t> read_msg(uint8_t address);
  void enrol_device_(std::vector<uint8_t> &data);
  void process_data_(ByteBuffer &buffer);
  void dump_config() override;

  void add_device(SicomDevice *device) { this->devices_.push_back(device); }
  void set_debug(bool debug) { this->debug_ = debug; }
  void set_tx_pin(uint8_t tx_pin) { this->tx_pin_ = tx_pin; }
  void set_tx_enable_pin(GPIOPin *tx_enable_pin) { this->tx_enable_pin_ = tx_enable_pin; }

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
  enum State state_{STATE_ALL_CALL};
};

}  // namespace sicom
}  // namespace esphome

#endif
