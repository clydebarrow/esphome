//
// Created by Clyde Stubbs on 12/9/2023.
//

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/display.h"
#include "../../../.pio/libdeps/esp32-idf/lvgl/src/lvgl.h"


namespace esphome {
namespace lvgl {

typedef lv_obj_t LvglObj;

class LvglComponent : public Component {

 protected:
  display::DisplayBuffer *display_{};


};

}  // namespace lvgl
}  // namespace esphome

