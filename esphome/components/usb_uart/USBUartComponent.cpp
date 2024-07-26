#include "USBUartComponent.h"

namespace esphome {
namespace usb_uart {
void USBUartComponent::setup() { USBClient::setup(); }
void USBUartComponent::loop() { USBClient::loop(); }
void USBUartTypeCH344::on_disconnected_() {}
void USBUartTypeCH344::on_connected_() {
  ESP_LOGD(TAG, "on_connected");
  this->control_transfer_(
      usb_host::USB_RECIP_DEVICE | usb_host::USB_DIR_IN | usb_host::USB_TYPE_VENDOR, true, 0x5F, 0, 0,
      [=](usb_host::transfer_status_t &status) {
        ESP_LOGD(TAG, "Transfer result: %X %X", status.data[0], status.data[1]);
      },
      2);
}

}  // namespace usb_uart
}  // namespace esphome