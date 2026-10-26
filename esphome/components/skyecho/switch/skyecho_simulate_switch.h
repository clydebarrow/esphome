#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "../skyecho.h"

namespace esphome {
namespace skyecho {

class SkyEchoSimulateSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(SkyEcho *parent) { this->parent_ = parent; }
  void setup() override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;
  SkyEcho *parent_{nullptr};
};

}  // namespace skyecho
}  // namespace esphome
