#pragma once

#include "../goodwe.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome::goodwe {
template<size_t OFFSET, size_t LENGTH> class TextSensorParameter : public Parameter, public text_sensor::TextSensor {
 public:
  TextSensorParameter(const char *type) : Parameter(type) {}
  ~TextSensorParameter() override = default;

  void decode(bytebuffer::ByteBuffer &data) override {
    if (data.get_limit() < OFFSET + LENGTH) {
      ESP_LOGW(TAG, "Buffer too small");
      this->publish_state("Unknown");
      return;
    }
    std::array<uint8_t, LENGTH> bytes{};
    data.get_bytes(bytes.data(), LENGTH, OFFSET);
    this->publish_state({bytes.begin(), bytes.end()});
  }

 protected:
  void dump_config() override { LOG_TEXT_SENSOR("    ", this->type_, this); }
};

template<size_t OFFSET> class OptionSensorParameter : public Parameter, public text_sensor::TextSensor {
 public:
  OptionSensorParameter(const char *type) : Parameter(type) {}
  ~OptionSensorParameter() override = default;

  void decode(bytebuffer::ByteBuffer &data) override {
    if (data.get_limit() <= OFFSET) {
      ESP_LOGW(TAG, "Buffer too small");
      this->publish_state("Unknown");
      return;
    }
    auto value = data.get_uint8(OFFSET);
    if (value >= this->options_.size()) {
      ESP_LOGW(TAG, "Invalid value for options %u at offset %zu", value, OFFSET);
      this->publish_state("Unknown");
    } else {
      this->publish_state(this->options_.at(value));
    }
  }

  void add_option(const char *option) { this->options_.push_back(option); }

 protected:
  void dump_config() override { LOG_TEXT_SENSOR("    ", this->type_, this); }
  std::vector<const char *> options_{};
};
}  // namespace esphome::goodwe
