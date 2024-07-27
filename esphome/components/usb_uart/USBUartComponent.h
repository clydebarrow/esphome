#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart_component.h"
#include "esphome/components/usb_host/usb_host.h"
namespace esphome {
namespace usb_uart {
class USBUartTypeCdcAcm;
class USBUartComponent;

static const char *TAG = "usb_uart";

typedef struct {
  const usb_ep_desc_t *notify_ep;
  const usb_ep_desc_t *in_ep;
  const usb_ep_desc_t *out_ep;
  const usb_intf_desc_t *intf;
} cdc_eps_t;

class USBUartChannel : public uart::UARTComponent, public Parented<USBUartComponent> {
  friend class USBUartComponent;
  friend class USBUartTypeCdcAcm;

 public:
  USBUartChannel(uint8_t index) : index_(index) {}
  void write_array(const uint8_t *data, size_t len) override{};
  bool peek_byte(uint8_t *data) override { return false; };
  bool read_array(uint8_t *data, size_t len) override { return false; }
  int available() override { return 0; }
  void flush() override {}
  void check_logger_conflict() override {}
  void start_input();

 protected:
  const uint8_t index_;
  bool input_started_{true};
  cdc_eps_t cdc_dev_{};
};

class USBUartComponent : public usb_host::USBClient {
 public:
  USBUartComponent(uint16_t vid, uint16_t pid) : usb_host::USBClient(vid, pid) {}
  void setup() override;
  void loop() override;

  void add_channel(uart::UARTComponent *channel) { this->channels_.push_back(channel); }

 protected:
  std::vector<USBUartChannel *> channels_{};
};

class USBUartTypeCdcAcm : public USBUartComponent {
 public:
  USBUartTypeCdcAcm(uint16_t vid, uint16_t pid) : USBUartComponent(vid, pid) {}

 protected:
  void on_connected_() override;
  void on_disconnected_() override;
  void start_input_(USBUartChannel *channel);
};

}  // namespace usb_uart
}  // namespace esphome
