#pragma once

#include "../goodwe.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome::goodwe {

template<size_t OFFSET> class TextSensorParameter : public Parameter, public text_sensor::TextSensor {
 public:
  TextSensorParameter(const char *type) : Parameter(type) {}

  void decode(bytebuffer::ByteBuffer &data) override {
    auto value = data.get_uint8(OFFSET);
    if (value >= this->options_.size()) {
      ESP_LOGW(TAG, "Invalid value for options %u at offset %zu", value, OFFSET);
      this->publish_state("Unknown");
    } else {
      this->publish_state(this->options_.at(value));
    }
  }

  void add_option(const char *option) { this->options_.push_back(option); }

  void dump_config() override { LOG_TEXT_SENSOR("    ", this->type_, this); }

 protected:
  std::vector<const char *> options_{};
};

}  // namespace esphome::goodwe
