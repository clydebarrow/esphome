#pragma once

#include "esphome/components/bytebuffer/bytebuffer.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/transport/transport.h"

#include <map>

namespace esphome {
namespace goodwe {

static const char *TAG = "goodwe";

/**
 * Base class for parameters read using Goodwe protocol.
 */
class Parameter {
 public:
  Parameter(const char *type) : type_(type) {}

  virtual ~Parameter() = default;
  /**
   * Process the data received from the Goodwe device
   *
   * @param data The byte buffer containing the data received from the Goodwe device
   */
  virtual void decode(bytebuffer::ByteBuffer &data) = 0;
  virtual void dump_config() = 0;

 protected:
  const char *type_;
};

class Goodwe : public PollingComponent {
 public:
  Goodwe(transport::Transport *transport) : transport_(transport){};
  virtual ~Goodwe() = default;

  void setup() override {
    this->transport_->add([this](const std::vector<uint8_t> &data) { this->on_receive_(data); });
  }
  void update() override {
    ESP_LOGD(TAG, "Goodwe::update");
    this->transport_->transmit({0xAA, 0x55, 0xC0, 0x7F, 0x01, 0x06, 0x00, 0x02, 0x45});
  }

  void loop() override{};
  void add_parameter(uint16_t code, Parameter *parameter) {
    ESP_LOGD(TAG, "Adding parameter with code %04X", code);
    this->parameters_[code].push_back(parameter);
  }

  void dump_config() override;

 protected:
  transport::Transport *transport_;
  std::vector<uint8_t> buffer_;
  std::map<uint16_t, std::vector<Parameter *>> parameters_{};

  void on_receive_(const std::vector<uint8_t> &data);
  void process_data_();
};

}  // namespace goodwe
}  // namespace esphome
