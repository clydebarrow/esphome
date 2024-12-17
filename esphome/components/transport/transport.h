#pragma once

#include "esphome/core/helpers.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace transport {

static const char *TAG = "transport";

/**
 * A class that will transport data to and from an abstract channel.
 *
 * The operations defined are:
 *
 * Transmit: the function transmit() takes a vector of bytes, and returns when all bytes are queued for
 * transmission at least.
 *
 * Receive: The callbacks for an instance of this class are called with currently received data. The callee is
 * expected to remove consumed data from the vector (passed by reference.)
 */
class Transport : public CallbackManager<void(const std::vector<uint8_t> &)>, public Component {
 public:
  virtual bool transmit(const std::vector<uint8_t> &data) {
    auto result = this->send_data_(data);
    ESP_LOGV(TAG, "send_data returns %s for data  %s", TRUEFALSE(result),
             format_hex_pretty(data.data(), data.size()).c_str());
    return result;
  }

 protected:
  void on_receive_data_(const std::vector<uint8_t> &data) {
    ESP_LOGV(TAG, "Received data %s", format_hex_pretty(data.data(), data.size()).c_str());
    this->call(data);
  }
  virtual ~Transport() = default;

  virtual bool send_data_(const std::vector<uint8_t> &data) = 0;
};
}  // namespace transport
}  // namespace esphome
