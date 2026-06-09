#pragma once

#include "esphome/core/component.h"
#include "esphome/components/output/float_output.h"
#include "axp2101.h"

namespace esphome::axp2101 {

// A float output driving the voltage of a single power rail.
class AXP2101Output : public output::FloatOutput, public Parented<AXP2101> {
 public:
  void set_channel(Channel channel) { this->channel_ = channel; }

 protected:
  void write_state(float state) override { this->parent_->set_rail_level(this->channel_, state); }

  Channel channel_{Channel::DCDC1};
};

}  // namespace esphome::axp2101
