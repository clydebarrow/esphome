#pragma once

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "../skyecho.h"

namespace esphome {
namespace skyecho {

class SkyEchoTextSensor : public text_sensor::TextSensor, public PollingComponent {
 public:
  void set_parent(SkyEcho *parent) { this->parent_ = parent; }
  void setup() override {}
  void dump_config() override;
  void update() override;

 protected:
  SkyEcho *parent_{nullptr};
  char nmea_buf_[256];

  // NMEA sentence generators
  void generate_gpgga(std::string &output);
  void generate_gprmc(std::string &output);
  void generate_pgrmz(std::string &output);
  void generate_pflaa(std::string &output);

  // Helper functions
  uint8_t calculate_checksum(const char *sentence);
  void add_checksum(std::string &sentence);
  int get_accuracy(int nacP);
};

}  // namespace skyecho
}  // namespace esphome
