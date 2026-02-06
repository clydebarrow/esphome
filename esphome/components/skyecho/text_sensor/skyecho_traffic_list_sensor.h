#pragma once

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "../skyecho.h"
#include "../traffic_manager.h"

namespace esphome {
namespace skyecho {

class SkyEchoTrafficListSensor : public text_sensor::TextSensor, public PollingComponent {
 public:
  void set_parent(SkyEcho *parent) { this->parent_ = parent; }
  void setup() override {}
  void dump_config() override;
  void update() override;

 protected:
  SkyEcho *parent_{nullptr};

  static const char *get_category_name(AircraftCategory category);
  static const char *get_source_name(TrafficSource source);
};

}  // namespace skyecho
}  // namespace esphome
