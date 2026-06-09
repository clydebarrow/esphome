#include "axp2101_switch.h"

namespace esphome::axp2101 {

void AXP2101Switch::write_state(bool state) {
  if (this->charge_mode_) {
    this->parent_->set_charging_enabled(state);
  } else {
    this->parent_->set_rail_enabled(this->channel_, state);
  }
  this->publish_state(state);
}

}  // namespace esphome::axp2101
