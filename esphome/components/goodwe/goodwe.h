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

  void setup() override { this->transport_->add(this->on_receive_); }
  void update() override {}
  void loop() override;

 protected:
  transport::Transport *transport_;

  void on_receive_(const std::vector<uint8_t> &data) { ESP_LOGD(TAG, "Received %s", format_hex_pretty(data).c_str()); }
};

}  // namespace goodwe
}  // namespace esphome
