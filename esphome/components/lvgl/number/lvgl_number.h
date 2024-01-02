#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace lvgl {

class LVGLNumber : public number::Number {
 public:
  void set_control_lambda(std::function<void(float)> control_lambda) { this->control_lambda_ = control_lambda; }

 protected:
  void control(float value) override;
  std::function<void(float)> control_lambda_{};
};

}  // namespace lvgl
}  // namespace esphome
