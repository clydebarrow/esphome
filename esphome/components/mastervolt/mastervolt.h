#pragma once

#include "esphome/core/component.h"
#include "esphome/components/canbus/canbus.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace mastervolt {

static const char *const TAG = "mastervolt";

class MastervoltSensor: public sensor::Sensor {
  public:
  MastervoltSensor(uint16_t message_id, uint16_t offset): message_id_(message_id), offset_(offset) {}

  protected:
   uint16_t message_id_;
   uint16_t offset_;
};

class MastervoltDevice {
 public:
  MastervoltDevice(uint32_t can_id): can_id_(can_id) {}
  protected:
  uint32_t can_id_;
};

class Mastervolt: public Component {
 public:
  Mastervolt(canbus::Canbus *canbus): canbus_(canbus) {}
  void setup() override {};

  void add_device(MastervoltDevice *device) {
    devices_.push_back(device);
  }

  protected:
  canbus::Canbus *canbus_;
  std::vector<MastervoltDevice *> devices_{};

};

}  // namespace mastervolt
}  // namespace esphome
