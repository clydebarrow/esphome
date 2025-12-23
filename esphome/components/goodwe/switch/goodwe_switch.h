#pragma once

#pragma once

#include "../goodwe.h"
#include "esphome/components/switch/switch.h"

namespace esphome::goodwe {

template<uint16_t OFFSET, uint16_t WRITE_CMD, uint16_t OFF_DATA, uint16_t ON_DATA, uint16_t READ_DATA>
class SwitchParameter : public Setting, public switch_::Switch {
 public:
  SwitchParameter(const char *type) : Setting(type, WRITE_CMD) {}

  std::vector<uint8_t> get_data() override {
    auto data = this->new_state_ ? ON_DATA : OFF_DATA;
    return std::vector{(uint8_t) (data >> 8), (uint8_t) data};
  }

  bool should_send() override { return this->dirty_; }
  void dump_config() override { LOG_SWITCH("    ", this->type_, this); }

 protected:
  void decode(bytebuffer::ByteBuffer &data) override {
    auto intdata = data.get<uint16_t>(OFFSET);
    auto value = (intdata & READ_DATA) != 0;
    if (value == this->new_state_) {
      this->publish_state(value);
      this->dirty_ = false;
    }
    if (value != this->state) {
      this->publish_state(value);
    }
    ESP_LOGD(TAG, "Decoded switch value: 0x%04X/%s for parameter at offset %u", intdata, TRUEFALSE(value), OFFSET);
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
