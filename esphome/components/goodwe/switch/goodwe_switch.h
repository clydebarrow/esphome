#pragma once

#pragma once

#include "../goodwe.h"
#include "esphome/components/switch/switch.h"

namespace esphome::goodwe {

template<uint16_t OFFSET, uint16_t WRITE_CMD, uint16_t LENGTH, uint16_t DATA>
class SwitchParameter : public Setting, public switch_::Switch {
 public:
  SwitchParameter(const char *type) : Setting(type, WRITE_CMD) {}

  std::vector<uint8_t> get_data() override {
    if (LENGTH == 1)
      return std::vector{(uint8_t) (this->new_state_ ? DATA : 0x00)};
    if (LENGTH == 2)
      if (this->new_state_)
        return std::vector{(uint8_t) (DATA >> 8), (uint8_t) DATA};
    return std::vector<uint8_t>{0, 0};
  }

  bool should_send() override { return this->dirty_; }
  void dump_config() override { LOG_SWITCH("    ", this->type_, this); }

 protected:
  void decode(bytebuffer::ByteBuffer &data) override {
    bool value;
    if (LENGTH == 1)
      value = data.get<uint8_t>(OFFSET) != 0;
    else
      value = data.get<uint16_t>(OFFSET) != 0;
    if (value == this->new_state_) {
      this->dirty_ = false;
    }
    if (value != this->state) {
      this->publish_state(value);
    }
    ESP_LOGD(TAG, "Decoded switch value: %s for parameter at offset %04X", TRUEFALSE(value), OFFSET);
  }

  void write_state(bool value) override {
    if (value != this->state) {
      this->new_state_ = value;
      this->dirty_ = true;
    }
  }

  bool new_state_{};
  bool dirty_{};
};
}  // namespace esphome::goodwe
