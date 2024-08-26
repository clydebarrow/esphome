#include "USBUartComponent.h"
#include "usb/usb_host.h"
#include "esphome/core/log.h"

#include "esphome/core/bytebuffer.h"

namespace esphome {
namespace usb_uart {

/**
 * CH34x
 */

void USBUartTypeCH34X::enable_channels_() {
  // enable the channels
  for (auto channel : this->channels_) {
    if (!channel->initialised_)
      continue;
    usb_host::transfer_cb_t callback = [=](const usb_host::transfer_status_t &status) {
      if (!status.success) {
        ESP_LOGE(TAG, "Control transfer failed, status=%s", esp_err_to_name(status.error_code));
        channel->initialised_ = false;
      }
    };

    uint8_t divisor = 7;
    uint32_t clk = 12000000;

    auto baud_rate = channel->baud_rate_;
    if (baud_rate < 256000) {
      if (baud_rate > 6000000 / 255) {
        divisor = 3;
        clk = 6000000;
      } else if (baud_rate > 750000 / 255) {
        divisor = 2;
        clk = 750000;
      } else if (baud_rate > 93750 / 255) {
        divisor = 1;
        clk = 93750;
      } else {
        divisor = 0;
        clk = 11719;
      }
    }
    ESP_LOGV(TAG, "baud_rate: %" PRIu32 ", divisor: %d, clk: %" PRIu32, baud_rate, divisor, clk);
    auto factor = static_cast<uint8_t>(clk / baud_rate);
    if (factor == 0 || factor == 0xFF) {
      ESP_LOGE(TAG, "Invalid baud rate %" PRIu32, baud_rate);
      channel->initialised_ = false;
      continue;
    }
    if ((clk / factor - baud_rate) > (baud_rate - clk / (factor + 1)))
      factor++;
    factor = 256 - factor;

    uint8_t value = 0xC0;
    if (channel->stop_bits_ == UART_CONFIG_STOP_BITS_2)
      value |= 4;
    switch (channel->parity_) {
      case UART_CONFIG_PARITY_NONE:
        break;
      default:
        value |= 8 | ((channel->parity_ - 1) << 4);
        break;
    }
    value |= channel->data_bits_ - 5;
    value <<= 8;
    value |= 0x8C;
    uint8_t cmd = 0xA1 + channel->index_;
    if (channel->index_ >= 2)
      cmd += 0xE;
    this->control_transfer(USB_VENDOR_DEV | usb_host::USB_DIR_OUT, cmd, value, (factor << 8) | divisor, callback);
  }
  USBUartTypeCdcAcm::enable_channels_();
}
}  // namespace usb_uart
}  // namespace esphome
