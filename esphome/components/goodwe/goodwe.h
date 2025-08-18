#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/transport/transport.h"

namespace esphome {
namespace goodwe {

static const char *TAG = "goodwe";

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

 protected:
  transport::Transport *transport_;
  std::vector<uint8_t> buffer_;

  void on_receive_(const std::vector<uint8_t> &data);
  void process_data_();
};

}  // namespace goodwe
}  // namespace esphome
