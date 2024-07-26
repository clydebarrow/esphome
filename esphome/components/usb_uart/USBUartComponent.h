#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart_component.h"
#include "esphome/components/usb_host/usb_host.h"
namespace esphome {
namespace usb_uart {

static const char *TAG = "usb_uart";
class USBUartComponent : public usb_host::USBClient {
 public:
  USBUartComponent(uint16_t vid, uint16_t pid) : usb_host::USBClient(vid, pid) {}
  void setup() override;
  void loop() override;

  void add_channel(uint8_t index, uart::UARTComponent *channel) { this->channels_[index] = channel; }

 protected:
  std::map<uint8_t, uart::UARTComponent *> channels_{};
};

class USBUartTypeCH344 : public USBUartComponent {
 public:
  USBUartTypeCH344(uint16_t vid, uint16_t pid) : USBUartComponent(vid, pid) {}

 protected:
  void on_connected_() override;
  void on_disconnected_() override;
};

class USBUartChannel : public uart::UARTComponent, public Parented<USBUartComponent> {
 public:
  USBUartChannel(uint8_t index) : index_(index) {}
  void write_array(const uint8_t *data, size_t len) override{};
  bool peek_byte(uint8_t *data) override { return false; };
  bool read_array(uint8_t *data, size_t len) override { return false; }
  int available() override { return 0; }
  void flush() override {}
  void check_logger_conflict() override {}

 protected:
  const uint8_t index_;
};

}  // namespace usb_uart
}  // namespace esphome
