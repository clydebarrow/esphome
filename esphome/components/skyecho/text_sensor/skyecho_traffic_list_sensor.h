#pragma once

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "../skyecho.h"

namespace esphome {
namespace skyecho {

class SkyEchoTrafficListSensor : public text_sensor::TextSensor, public PollingComponent {
 public:
  void set_parent(SkyEcho *parent) { this->parent_ = parent; }
  void setup() override {}
  void dump_config() override;
  void update();

 protected:
  SkyEcho *parent_{nullptr};

  const char *get_category_name(emitterCategory_t category);
};

}  // namespace skyecho
}  // namespace esphome
