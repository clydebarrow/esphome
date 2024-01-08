#pragma once

#if LVGL_USES_SWITCH
#include "esphome/components/switch/switch.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace lvgl {

class LVGLSwitch : public switch_::Switch {
 public:
  void set_state_lambda(std::function<void(bool)> state_lambda) { this->state_lambda_ = state_lambda; }

 protected:
  void write_state(bool value) { this->state_lambda_(value); }
  std::function<void(bool)> state_lambda_{};
};

}  // namespace lvgl
}  // namespace esphome
#endif
