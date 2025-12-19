#pragma once

#include "../goodwe.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome::goodwe {
template<uint16_t OFFSET> class BinarySensorParameter : public Parameter, public binary_sensor::BinarySensor {
 public:
  BinarySensorParameter(const char *type) : Parameter(type) {}

  void decode(bytebuffer::ByteBuffer &data) override {
    auto value = data.get<uint16_t>(OFFSET);
    this->publish_state(value != 0);
    ESP_LOGV(TAG, "Decoded binary sensor value: %s for parameter at offset %04X", TRUEFALSE(value), OFFSET);
  }

  void dump_config() override { LOG_BINARY_SENSOR("    ", this->type_, this); }
};
}  // namespace esphome::goodwe
