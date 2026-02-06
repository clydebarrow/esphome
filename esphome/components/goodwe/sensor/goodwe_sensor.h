#pragma once

#include "../goodwe.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome::goodwe {

template<typename T, uint16_t OFFSET, float SCALE> class SensorParameter : public Parameter, public sensor::Sensor {
  static_assert(std::is_floating_point_v<T> || std::is_integral_v<T>, "T must be a floating point or integral type");
  static_assert(sizeof(T) <= sizeof(uint32_t), "T must be 32 bits or less");

 public:
  SensorParameter(const char *type) : Parameter(type) {}

  void decode(bytebuffer::ByteBuffer &data) override {
    auto value = data.get<T>(OFFSET);
    this->publish_state(value * SCALE);
    ESP_LOGV(TAG, "Decoded sensor value: %f for parameter at offset %04X", value * SCALE, OFFSET);
  }

  void dump_config() override { LOG_SENSOR("    ", this->type_, this); }
};

template<uint16_t DATA_OFFSET, uint16_t SIGN_OFFSET, float SCALE>
class BatteryCurrentParameter : public SensorParameter<uint16_t, DATA_OFFSET, SCALE> {
 public:
  BatteryCurrentParameter(const char *type) : SensorParameter<uint16_t, DATA_OFFSET, SCALE>(type) {}

  void decode(bytebuffer::ByteBuffer &data) override {
    int value = data.get_uint16(DATA_OFFSET);
    if (data.get_uint8(SIGN_OFFSET) == 0x02) {
      value = -value;
    }
    this->publish_state(value * SCALE);
    ESP_LOGV(TAG, "Decoded battery current value: %f for parameter at offset %04X", value * SCALE, DATA_OFFSET);
  }
};

class ComputedSensorParameter : public sensor::Sensor, public Parameter {
 public:
  ComputedSensorParameter(const char *type, std::function<float()> compute_value)
      : Parameter(type), compute_value_(compute_value) {}

  void decode(bytebuffer::ByteBuffer &data) override {
    float value = this->compute_value_();
    this->publish_state(value);
    ESP_LOGV(TAG, "Computed sensor value: %f for parameter type %s", value, this->type_);
  }

  void dump_config() override { LOG_SENSOR("    ", this->type_, this); }

 protected:
  std::function<float()> compute_value_;
};

}  // namespace esphome::goodwe
