#include "battery_gauge_sensor.h"
#include "esphome/core/log.h"
#include "core/hal.h"

namespace esphome {
namespace battery_gauge {

static const char *const TAG = "battery_gauge.sensor";

void BatteryGaugeSensor::on_current_(float value) {
  auto current = (value + this->last_current_) / 2.0f;
  this->last_current_ = value;
  auto now = millis();
  float interval = (now - this->last_time_) / 1000.0f / 3600.0f;
  this->last_time_ = now;
  this->publish_(this->charge_state_ + current * interval);
}

void BatteryGaugeSensor::publish_(float new_state) {
  this->charge_state_ = std::max(0.0f, std::min(new_state, this->capacity_));
  auto percentage = this->charge_state_ / this->capacity_ * 100.0f;
  this->publish_state(percentage);
  unsigned new_percentage = std::round(percentage);
  if (new_percentage != this->charge_percentage_) {
    this->charge_percentage_ = new_percentage;
    this->saved_percentage_.save(&new_percentage);
  }
}
void BatteryGaugeSensor::on_voltage_(float value) {}
void BatteryGaugeSensor::setup() {
  this->current_source_->add_on_state_callback([this](float value) { this->on_current_(value); });
  this->voltage_source_->add_on_state_callback([this](float value) { this->on_voltage_(value); });
  this->last_time_ = millis();
  this->saved_percentage_.load(&this->charge_percentage_);
}

void BatteryGaugeSensor::dump_config() {
  LOG_SENSOR("", "Battery Gauge", this);
  ESP_LOGCONFIG(TAG, "Capacity: %.0f", this->capacity_);
}  // namespace copy
}  // namespace esphome
