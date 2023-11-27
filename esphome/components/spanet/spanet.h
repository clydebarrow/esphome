#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace spanet {

static const size_t BUF_LEN = 512;    // length of rx buffer
static const size_t MAX_FIELDS = 40;  // maximum fields to decode

class Spanet : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; };

  void set_temperature_sensor(sensor::Sensor sens) { this->temperature_sensor_ = sens; }

 protected:
  void process_buffer_();
  char rxbuf_[BUF_LEN];  // uart line buffer
  size_t rxcnt_{};
  const char *fields_[MAX_FIELDS];  // stores extracted fields
  size_t fieldcnt_{};

  // sensors

  sensor::Sensor temperature_sensor_{};
};

}  // namespace spanet
}  // namespace esphome
