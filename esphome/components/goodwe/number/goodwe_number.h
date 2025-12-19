#pragma once

#pragma once

#include "../goodwe.h"
#include "esphome/components/number/number.h"

namespace esphome::goodwe {

template<uint16_t OFFSET, uint16_t WRITE_CMD> class NumberParameter : public Setting, public number::Number {
 public:
  NumberParameter(const char *type) : Setting(type, WRITE_CMD) {}

  std::vector<uint8_t> get_data() override {
    auto value = (int16_t) this->new_state_;
    ESP_LOGD(TAG, "State %f, value %d", this->new_state_, value);
    return std::vector{(uint8_t) (value >> 8), (uint8_t) value};
  }

  bool should_send() override {
    if (!std::isfinite(this->new_state_) || !this->has_state())
      return false;
    return this->new_state_ != this->state;
  }
  void dump_config() override { LOG_NUMBER("    ", this->type_, this); }

 protected:
  void decode(bytebuffer::ByteBuffer &data) override {
    auto value = data.get<uint16_t>(OFFSET);
    ESP_LOGD(TAG, "Decoded number value: %d for parameter at offset %04X", value, OFFSET);
    if (value != this->state) {
      this->publish_state(value);
      this->new_state_ = value;
    }
  }

  void control(float value) override { this->new_state_ = value; }

  float new_state_{NAN};
};
}  // namespace esphome::goodwe
