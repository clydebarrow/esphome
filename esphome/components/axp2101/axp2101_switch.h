#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "axp2101.h"

namespace esphome::axp2101 {

// A switch controlling either the charger enable or a single power rail.
class AXP2101Switch : public switch_::Switch, public Parented<AXP2101> {
 public:
  void set_channel(Channel channel) { this->channel_ = channel; }
  void set_charge_mode() { this->charge_mode_ = true; }

 protected:
  void write_state(bool state) override;

  Channel channel_{Channel::DCDC1};
  bool charge_mode_{false};
};

}  // namespace esphome::axp2101
