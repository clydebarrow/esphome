#include "axp2101.h"
#include "esphome/core/log.h"

namespace esphome::axp2101 {

static const char *const TAG = "axp2101";

// PMU status registers
static constexpr uint8_t REG_STATUS1 = 0x00;      // VBUS good (bit5), battery present (bit3)
static constexpr uint8_t REG_STATUS2 = 0x01;      // charge direction (bits 6:5), charger status (bits 2:0)
static constexpr uint8_t REG_CHARGE_CTRL = 0x18;  // module enable; charger enable on bit1
static constexpr uint8_t CHARGE_ENABLE_BIT = 1;
static constexpr uint8_t REG_ADC_ENABLE = 0x30;   // per-channel ADC enable
static constexpr uint8_t REG_ADC_VBAT = 0x34;     // 13-bit, mV
static constexpr uint8_t REG_ADC_VBUS = 0x38;     // 14-bit, mV
static constexpr uint8_t REG_ADC_VSYS = 0x3A;     // 14-bit, mV
static constexpr uint8_t REG_ADC_TDIE = 0x3C;     // 14-bit, die temperature raw
static constexpr uint8_t REG_BAT_PERCENT = 0xA4;  // fuel gauge, 0-100%

// ADC enable bits: VBAT(0), VBUS(2), VSYS(3), die temperature(5)
static constexpr uint8_t ADC_ENABLE_MASK = 0x2D;

// STATUS2 charge direction (bits 6:5): 1 == charging
static constexpr uint8_t CHARGE_DIRECTION_CHARGING = 0x01;
// STATUS2 charger status (bits 2:0): 4 == charge done
static constexpr uint8_t CHARGER_STATUS_DONE = 0x04;

// Rail descriptions, indexed by Channel. volt_reg == 0 means no voltage control.
const std::array<RailInfo, NUM_CHANNELS> AXP2101::RAILS = {{
    {0x80, 0, 0x82, 1500, 3400, 100},  // DCDC1
    {0x80, 1, 0x00, 0, 0, 0},          // DCDC2 (piecewise voltage, enable only)
    {0x80, 2, 0x00, 0, 0, 0},          // DCDC3 (piecewise voltage, enable only)
    {0x80, 3, 0x00, 0, 0, 0},          // DCDC4 (piecewise voltage, enable only)
    {0x80, 4, 0x86, 1400, 3700, 100},  // DCDC5
    {0x90, 0, 0x92, 500, 3500, 100},   // ALDO1
    {0x90, 1, 0x93, 500, 3500, 100},   // ALDO2
    {0x90, 2, 0x94, 500, 3500, 100},   // ALDO3
    {0x90, 3, 0x95, 500, 3500, 100},   // ALDO4
    {0x90, 4, 0x96, 500, 3500, 100},   // BLDO1
    {0x90, 5, 0x97, 500, 3500, 100},   // BLDO2
    {0x90, 7, 0x99, 500, 3400, 100},   // DLDO1
    {0x91, 0, 0x9A, 500, 1400, 50},    // DLDO2
}};

void AXP2101::setup() {
  // Enable the ADC channels we read in update()
  if (!this->write_byte(REG_ADC_ENABLE, ADC_ENABLE_MASK)) {
    ESP_LOGE(TAG, "Communication with AXP2101 failed");
    this->mark_failed();
    return;
  }
}

uint16_t AXP2101::read_adc_(uint8_t reg, uint8_t mask) {
  uint8_t buf[2];
  if (!this->read_bytes(reg, buf, 2))
    return 0;
  return (static_cast<uint16_t>(buf[0] & mask) << 8) | buf[1];
}

void AXP2101::update() {
  uint8_t status1, status2;
  if (!this->read_byte(REG_STATUS1, &status1) || !this->read_byte(REG_STATUS2, &status2)) {
    ESP_LOGW(TAG, "Failed to read status registers");
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();

  const uint8_t charge_direction = (status2 >> 5) & 0x03;
  const uint8_t charger_status = status2 & 0x07;

#ifdef USE_BINARY_SENSOR
  if (this->charging_binary_sensor_ != nullptr)
    this->charging_binary_sensor_->publish_state(charge_direction == CHARGE_DIRECTION_CHARGING);
  if (this->battery_present_binary_sensor_ != nullptr)
    this->battery_present_binary_sensor_->publish_state((status1 >> 3) & 0x01);
  if (this->vbus_present_binary_sensor_ != nullptr)
    this->vbus_present_binary_sensor_->publish_state((status1 >> 5) & 0x01);
  if (this->charge_done_binary_sensor_ != nullptr)
    this->charge_done_binary_sensor_->publish_state(charger_status == CHARGER_STATUS_DONE);
#endif

#ifdef USE_SENSOR
  if (this->battery_voltage_sensor_ != nullptr)
    this->battery_voltage_sensor_->publish_state(this->read_adc_(REG_ADC_VBAT, 0x1F) / 1000.0f);
  if (this->vbus_voltage_sensor_ != nullptr)
    this->vbus_voltage_sensor_->publish_state(this->read_adc_(REG_ADC_VBUS, 0x3F) / 1000.0f);
  if (this->system_voltage_sensor_ != nullptr)
    this->system_voltage_sensor_->publish_state(this->read_adc_(REG_ADC_VSYS, 0x3F) / 1000.0f);
  if (this->temperature_sensor_ != nullptr) {
    const uint16_t raw = this->read_adc_(REG_ADC_TDIE, 0x3F);
    this->temperature_sensor_->publish_state(22.0f + (7274.0f - raw) / 20.0f);
  }
  if (this->battery_level_sensor_ != nullptr) {
    uint8_t percent;
    if (this->read_byte(REG_BAT_PERCENT, &percent))
      this->battery_level_sensor_->publish_state(percent);
  }
#endif
}

void AXP2101::set_charging_enabled(bool enabled) {
  uint8_t value;
  if (!this->read_byte(REG_CHARGE_CTRL, &value))
    return;
  if (enabled) {
    value |= (1 << CHARGE_ENABLE_BIT);
  } else {
    value &= ~(1 << CHARGE_ENABLE_BIT);
  }
  this->write_byte(REG_CHARGE_CTRL, value);
}

bool AXP2101::is_charging_enabled() {
  uint8_t value;
  if (!this->read_byte(REG_CHARGE_CTRL, &value))
    return false;
  return (value >> CHARGE_ENABLE_BIT) & 0x01;
}

void AXP2101::set_rail_enabled(Channel channel, bool enabled) {
  const RailInfo &rail = this->rail_info_(channel);
  uint8_t value;
  if (!this->read_byte(rail.enable_reg, &value))
    return;
  if (enabled) {
    value |= (1 << rail.enable_bit);
  } else {
    value &= ~(1 << rail.enable_bit);
  }
  this->write_byte(rail.enable_reg, value);
}

bool AXP2101::get_rail_enabled(Channel channel) {
  const RailInfo &rail = this->rail_info_(channel);
  uint8_t value;
  if (!this->read_byte(rail.enable_reg, &value))
    return false;
  return (value >> rail.enable_bit) & 0x01;
}

void AXP2101::set_rail_millivolts(Channel channel, uint16_t millivolts) {
  const RailInfo &rail = this->rail_info_(channel);
  if (rail.volt_reg == 0) {
    ESP_LOGW(TAG, "Rail %u does not support voltage control", static_cast<unsigned>(channel));
    return;
  }
  uint16_t clamped = std::min(std::max(millivolts, rail.min_mv), rail.max_mv);
  uint8_t reg_value = (clamped - rail.min_mv) / rail.step_mv;
  this->write_byte(rail.volt_reg, reg_value);
}

void AXP2101::set_rail_level(Channel channel, float level) {
  if (level <= 0.0f) {
    this->set_rail_enabled(channel, false);
    return;
  }
  const RailInfo &rail = this->rail_info_(channel);
  this->set_rail_millivolts(channel, rail.min_mv + static_cast<uint16_t>(level * (rail.max_mv - rail.min_mv)));
  this->set_rail_enabled(channel, true);
}

void AXP2101::dump_config() {
  ESP_LOGCONFIG(TAG, "AXP2101 PMIC:");
  LOG_I2C_DEVICE(this);
  LOG_UPDATE_INTERVAL(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication failed");
  }
}

}  // namespace esphome::axp2101
