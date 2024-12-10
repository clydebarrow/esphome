#pragma once

#include "esphome/core/helpers.h"

namespace esphome {
namespace transport {

class Transport : public CallbackManager<void(const std::vector<uint8_t> &)> {
  const char *TAG = "transport";

 public:
  bool transmit(const std::vector<uint8_t> &data);
  virtual ~Transport() = default;

 protected:
  void on_receive_data_(const std::vector<uint8_t> &data);
  virtual bool send_data_(const std::vector<uint8_t> &data) = 0;
};
}  // namespace transport
}  // namespace esphome
