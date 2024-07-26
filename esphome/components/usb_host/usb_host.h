#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <map>
#include <vector>
#include "usb/usb_host.h"

namespace esphome {
namespace usb_host {

static const char *TAG = "usb_host";

static const uint8_t USB_RECIP_DEVICE = 0;
static const uint8_t USB_TYPE_VENDOR = 0x02 << 5;
static const uint8_t USB_DIR_IN = 0x01 << 7;

// used to report a transfer status
typedef struct {
  bool success;
  uint16_t error_code;
  uint8_t *data;
  size_t data_len;
  uint8_t endpoint;
} transfer_status_t;

// callback function type.
typedef std::function<void(transfer_status_t &)> transfer_cb_t;

enum ClientState {
  USB_CLIENT_INIT = 0,
  USB_CLIENT_OPEN,
  USB_CLIENT_CLOSE,
  USB_CLIENT_GET_DESC,
  USB_CLIENT_GET_INFO,
  USB_CLIENT_CONNECTED,
};
class USBClient : public Component {
  friend class USBHost;

 public:
  USBClient(uint16_t vid, uint16_t pid) : vid_(vid), pid_(pid) {}

  void setup() override;
  void loop() override;
  // setup must happen after the host bus has been setup
  float get_setup_priority() const override { return setup_priority::IO; }
  void on_opened(uint8_t addr);
  void on_closed(usb_device_handle_t handle);
  void control_transfer_callback(usb_transfer_t *xfer);

 protected:
  void disconnect_() {
    usb_host_device_close(this->handle_, this->device_handle_);
    this->state_ = USB_CLIENT_INIT;
    this->on_disconnected_();
  }
  transfer_cb_t ctrl_transfer_client_callback_{};

  void control_transfer_(uint8_t type, bool data_in, uint8_t request, uint16_t value, uint16_t index,
                         transfer_cb_t callback, uint16_t length = 0, uint8_t *data = nullptr);

  virtual void on_connected_() {}
  virtual void on_disconnected_() {}

  usb_host_client_handle_t handle_{};
  usb_device_handle_t device_handle_{};
  int device_addr_{-1};
  int state_{USB_CLIENT_INIT};
  uint16_t vid_{};
  uint16_t pid_{};
  usb_transfer_t *ctrl_transfer_data_{};
  bool ctrl_transfer_busy_{};
};
class USBHost : public Component {
 public:
  float get_setup_priority() const override { return setup_priority::BUS; }
  void loop() override;
  void setup() override;

 protected:
  std::vector<USBClient *> clients_{};
};

}  // namespace usb_host
}  // namespace esphome