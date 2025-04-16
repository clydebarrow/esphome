#pragma once

#include "esphome/core/component.h"
#include "esphome/components/canbus/canbus.h"
#include "esphome/components/sensor/sensor.h"

#include "esphome/components/bytebuffer/bytebuffer.h"

#include <map>

namespace esphome {
namespace mastervolt {

static const char *const TAG = "mastervolt";

class MastervoltSensor : public sensor::Sensor {
 public:
  MastervoltSensor(uint16_t message_id, uint16_t offset) : message_id_(message_id), offset_(offset) {}

  uint16_t message_id() { return message_id_; }

  void process_message(bytebuffer::ByteBuffer data) {
    if (data.get_uint16(0) != message_id_)
      return;
    this->publish_state(data.get_float(this->offset_));
  }

 protected:
  uint16_t message_id_;
  uint16_t offset_;
};

class MastervoltDevice {
  friend class Mastervolt;

 public:
  MastervoltDevice(uint32_t can_id) : can_id_(can_id) {}

  void on_receive(bytebuffer::ByteBuffer data);
  void add_sensor(MastervoltSensor *sensor) { this->sensors_[sensor->message_id()] = sensor; }

 protected:
  uint32_t can_id_;
  std::map<uint16_t, MastervoltSensor *> sensors_;
};

class Mastervolt : public Component {
 public:
  Mastervolt(canbus::Canbus *canbus) : canbus_(canbus) {}
  void setup() override;

  void add_device(MastervoltDevice *device) { devices_.push_back(device); }

  void set_debug(bool debug) { this->debug_ = debug; }

 protected:
  void on_receive_(uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data);
  canbus::Canbus *canbus_;
  std::vector<MastervoltDevice *> devices_{};
  bool debug_{};
};

}  // namespace mastervolt
}  // namespace esphome
