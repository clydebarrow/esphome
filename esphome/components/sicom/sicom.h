#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace sicom {

class SicomComponent : public uart::UARTDevice, public PollingComponent {
public:
  void setup() override;
  void loop() override;
  void dump_config() override;
};


}  // namespace empty_uart_component
}  // namespace esphome
