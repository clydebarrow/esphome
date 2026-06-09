#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif

#include <array>

namespace esphome::axp2101 {

// Power rails. Order MUST match the CHANNELS map in __init__.py
enum class Channel : uint8_t {
  DCDC1 = 0,
  DCDC2,
  DCDC3,
  DCDC4,
  DCDC5,
  ALDO1,
  ALDO2,
  ALDO3,
  ALDO4,
  BLDO1,
  BLDO2,
  DLDO1,
  DLDO2,
  LAST,
};
static constexpr size_t NUM_CHANNELS = static_cast<size_t>(Channel::LAST);

// Static description of a power rail: which register/bit enables it and how its
// output voltage is encoded. A linear encoding is used: reg_value = (mv - min_mv) / step_mv.
// volt_reg == 0 means the rail has no (supported) voltage control.
struct RailInfo {
  uint8_t enable_reg;
  uint8_t enable_bit;
  uint8_t volt_reg;
  uint16_t min_mv;
  uint16_t max_mv;
  uint16_t step_mv;
};

class AXP2101 : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

#ifdef USE_SENSOR
  void set_battery_voltage_sensor(sensor::Sensor *s) { this->battery_voltage_sensor_ = s; }
  void set_battery_level_sensor(sensor::Sensor *s) { this->battery_level_sensor_ = s; }
  void set_vbus_voltage_sensor(sensor::Sensor *s) { this->vbus_voltage_sensor_ = s; }
  void set_system_voltage_sensor(sensor::Sensor *s) { this->system_voltage_sensor_ = s; }
  void set_temperature_sensor(sensor::Sensor *s) { this->temperature_sensor_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  void set_charging_binary_sensor(binary_sensor::BinarySensor *s) { this->charging_binary_sensor_ = s; }
  void set_battery_present_binary_sensor(binary_sensor::BinarySensor *s) { this->battery_present_binary_sensor_ = s; }
  void set_vbus_present_binary_sensor(binary_sensor::BinarySensor *s) { this->vbus_present_binary_sensor_ = s; }
  void set_charge_done_binary_sensor(binary_sensor::BinarySensor *s) { this->charge_done_binary_sensor_ = s; }
#endif

  // Charger control (used by the charge_enable switch)
  void set_charging_enabled(bool enabled);
  bool is_charging_enabled();

  // Power rail control (used by the rail switch and output platforms)
  void set_rail_enabled(Channel channel, bool enabled);
  bool get_rail_enabled(Channel channel);
  // Set the rail output voltage, clamped to the rail's supported range.
  void set_rail_millivolts(Channel channel, uint16_t millivolts);
  // Drive a rail from a 0..1 level (used by the output platform): 0 disables the
  // rail, otherwise the level is mapped across the rail's voltage range.
  void set_rail_level(Channel channel, float level);

 protected:
  uint16_t read_adc_(uint8_t reg, uint8_t mask);
  const RailInfo &rail_info_(Channel channel) { return RAILS[static_cast<size_t>(channel)]; }

  static const std::array<RailInfo, NUM_CHANNELS> RAILS;

#ifdef USE_SENSOR
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *battery_level_sensor_{nullptr};
  sensor::Sensor *vbus_voltage_sensor_{nullptr};
  sensor::Sensor *system_voltage_sensor_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *charging_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_present_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *vbus_present_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *charge_done_binary_sensor_{nullptr};
#endif
};

}  // namespace esphome::axp2101
