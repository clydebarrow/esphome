#include "lvgl_number.h"
#include "esphome/core/log.h"

namespace esphome {
namespace lvgl {

static const char *const TAG = "lvgl.number";


void LVGLNumber::control(float value) {
  this->control_lambda_(value);
}

}  // namespace lvgl
}  // namespace esphome
