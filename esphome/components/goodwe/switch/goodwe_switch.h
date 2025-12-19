#pragma once

#pragma once

#include "../goodwe.h"
#include "esphome/components/switch/switch.h"

namespace esphome::goodwe {

template<uint16_t OFFSET, uint16_t WRITE_CMD> class SwitchParameter : public Setting, public switch_::Switch {
 public:
  SwitchParameter(const char *type) : Setting(type, WRITE_CMD) {}

  std::vector<uint8_t> get_data() override { return {(uint8_t) (this->new_state_ ? 0x01 : 0x00)}; }

  bool should_send() override { return this->new_state_ != this->state; }
  void dump_config() override { LOG_SWITCH("    ", this->type_, this); }

 protected:
  void decode(bytebuffer::ByteBuffer &data) override {
    auto value = data.get<uint8_t>(OFFSET);
    this->publish_state(value != 0);
    ESP_LOGV(TAG, "Decoded switch value: %s for parameter at offset %04X", TRUEFALSE(value), OFFSET);
  }

  void write_state(bool value) override { this->new_state_ = value; }

  bool new_state_{};
};
}  // namespace esphome::goodwe
