#pragma once

#include "esphome/core/component.h"
#include "esphome/components/transport/transport.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace uart {

class UartTransport : public transport::Transport, public uart::UARTDevice {};

}  // namespace uart
}  // namespace esphome
