#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace sicom {

class SicomComponent : public uart::UARTDevice, public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void loop() override;
  void process_byte_(uint8_t byte);
  void dump_config() override;

 protected:
  std::vector<uint8_t> rx_buf_{};
  bool flag_7_seen_{};
  bool flag_8_seen_{};
  bool master_msg_{};
};

}  // namespace sicom
}  // namespace esphome
