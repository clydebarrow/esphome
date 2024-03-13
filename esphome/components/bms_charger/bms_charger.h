#pragma once

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/canbus/canbus.h"
#include "esphome/components/canbus_bms/canbus_bms.h"
#include "esphome/components/number/number.h"
#include <set>
#include <vector>
#include <map>

namespace esphome {
namespace bms_charger {

enum InverterProtocol {
  PROTOCOL_SMA,
  PROTOCOL_PYLON,
};

enum SwitchType {
  SW_FULL_CHARGE,
  SW_NO_CHARGE,
  SW_NO_DISCHARGE,
  SW_FORCE_CHARGE_1,
  SW_FORCE_CHARGE_2,
  SW_SUPPRESS_OVP,
};

class BmsChargerComponent;

class ParamNumber : public number::Number, public Component, public Parented<BmsChargerComponent> {
 public:
  void set_initial_value(float initial_value) { initial_value_ = initial_value; }

  void set_restore_value(bool restore_value) { this->restore_value_ = restore_value; }

  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void setup() override {
    float value;
    if (!this->restore_value_) {
      value = this->initial_value_;
    } else {
      this->pref_ = global_preferences->make_preference<float>(this->get_object_id_hash());
      if (!this->pref_.load(&value)) {
        if (!std::isnan(this->initial_value_)) {
          value = this->initial_value_;
        } else {
          value = this->traits.get_max_value();
        }
      }
    }
    this->publish_state(value);
  }

 protected:
  float initial_value_{NAN};
  bool restore_value_{false};
  ESPPreferenceObject pref_;

  void control(float value) override {
    if (this->restore_value_)
      this->pref_.save(&value);
    this->publish_state(value);
  }
};

class BmsSwitch : public switch_::Switch, public Component {
  friend class BmsChargerComponent;

 public:
  void setup() override { this->publish_state(this->state_); }

 protected:
  void write_state(bool state) override {
    this->state_ = state;
    this->publish_state(state);
  }

  bool state_{false};
};

class BatteryDesc {
  friend class BmsChargerComponent;

 public:
  BatteryDesc(canbus_bms::CanbusBmsComponent *battery, uint32_t heartbeat_id, const char *heartbeat_text)
      : battery_{battery}, heartbeat_id_{heartbeat_id}, heartbeat_text_{heartbeat_text} {}

 protected:
  canbus_bms::CanbusBmsComponent *battery_;
  uint32_t heartbeat_id_;
  const char *heartbeat_text_;
};

class BmsChargerComponent : public PollingComponent, public Action<std::vector<uint8_t>, uint32_t, bool> {
 public:
  BmsChargerComponent(const char *name, uint32_t timeout, canbus::Canbus *canbus, bool debug, uint32_t interval,
                      InverterProtocol protocol)
      : PollingComponent(interval),
        name_{name},
        timeout_{timeout},
        canbus_{canbus},
        debug_{debug},
        protocol_{protocol} {}

  // called when a CAN Bus message is received
  void play(std::vector<uint8_t> data, uint32_t can_id, bool remote_transmission_request) override;

  void update() override;

  void add_connectivity_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->connectivity_sensor_ = binary_sensor;
  }

  void add_switch(SwitchType type, BmsSwitch *sw) { this->switches_[type] = sw; }

  void add_battery(BatteryDesc *battery) { this->batteries_.push_back(battery); }

  void set_max_charge_current_number(ParamNumber *number) { this->max_charge_current_number_ = number; }

  void set_max_discharge_current_number(ParamNumber *number) { this->max_discharge_current_number_ = number; }

  void set_max_charge_voltage_number(ParamNumber *number) { this->max_charge_voltage_number_ = number; }
  void add_charge_point(float charge, int current) { this->charge_points_.push_back({charge, current}); }

 protected:
  const char *name_;
  uint32_t timeout_;
  canbus::Canbus *canbus_;
  bool debug_;
  enum InverterProtocol protocol_;
  uint32_t last_rx_ = 0;
  size_t counter_ = 0;
  std::vector<BatteryDesc *> batteries_;
  std::map<SwitchType, BmsSwitch *> switches_;
  binary_sensor::BinarySensor *connectivity_sensor_{};
  const ParamNumber *max_charge_current_number_{};
  const ParamNumber *max_discharge_current_number_{};
  const ParamNumber *max_charge_voltage_number_{};
  std::vector<std::pair<float, float>> charge_points_{};
  uint8_t last_charge_{};

  bool get_switch_state_(SwitchType type) { return this->switches_.count(type) != 0 && this->switches_[type]->state_; }
};

}  // namespace bms_charger
}  // namespace esphome
