#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "usb/usb_host.h"
#include <map>
#include <vector>

namespace esphome {
namespace usb_host {

static const char *TAG = "usb_host";

static std::string get_descriptor_string(const usb_str_desc_t *desc) {
  char buffer[256];
  char *p = buffer;
  for (size_t i = 0; i != desc->bLength / 2; i++) {
    auto c = desc->wData[i];
    if (c < 0x100)
      *p++ = c;
  }
  *p = '\0';
  return std::string(buffer);
}

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

  static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *ptr) {
    auto *client = (USBClient *) ptr;
    switch (event_msg->event) {
      case USB_HOST_CLIENT_EVENT_NEW_DEV: {
        auto addr = event_msg->new_dev.address;
        ESP_LOGD(TAG, "New device %d", event_msg->new_dev.address);
        if (client->state_ == USB_CLIENT_INIT) {
          client->device_addr_ = addr;
          client->state_ = USB_CLIENT_OPEN;
        }
        break;
      }
      case USB_HOST_CLIENT_EVENT_DEV_GONE: {
        if (client->device_handle_ == event_msg->dev_gone.dev_hdl) {
          client->device_handle_ = nullptr;
          client->state_ = USB_CLIENT_INIT;
          client->device_addr_ = -1;
        }
        break;
      }
    }
  }

 public:
  USBClient(uint16_t vid, uint16_t pid) : vid_(vid), pid_(pid) {}

  // setup must happen after the host bus has been setup
  float get_setup_priority() const override { return setup_priority::IO; }
  void setup() override {
    usb_host_client_config_t config{.is_synchronous = false,
                                    .max_num_event_msg = 5,
                                    .async = {.client_event_callback = client_event_cb, .callback_arg = this}};
    auto err = usb_host_client_register(&config, &this->handle_);
    if (err != ESP_OK) {
      ESP_LOGD(TAG, "client register failed: %s", esp_err_to_name(err));
      this->status_set_error("Client register failed");
      this->mark_failed();
    } else {
      ESP_LOGD(TAG, "client setup complete");
      usb_host_transfer_alloc(1024, 0, &this->transfer_);
    }
  }

  void loop() override {
    int err;
    switch (this->state_) {
      case USB_CLIENT_OPEN: {
        ESP_LOGD(TAG, "Open device %d", this->device_addr_);
        if ((err = usb_host_device_open(this->handle_, this->device_addr_, &this->device_handle_)) != ESP_OK) {
          ESP_LOGD(TAG, "Device open failed: %s", esp_err_to_name(err));
          this->state_ = USB_CLIENT_INIT;
          break;
        }
        ESP_LOGD(TAG, "Get descriptor device %d", this->device_addr_);
        const usb_device_desc_t *desc;
        if ((err = usb_host_get_device_descriptor(this->device_handle_, &desc)) != ESP_OK) {
          ESP_LOGD(TAG, "Device get_desc failed: %s", esp_err_to_name(err));
          this->disconnect_();
        } else {
          ESP_LOGD(TAG, "Device descriptor: vid %X pid %X", desc->idVendor, desc->idProduct);
          if (desc->idVendor == this->vid_ && desc->idProduct == this->pid_) {
            ESP_LOGD(TAG, "Getting info");
            usb_device_info_t dev_info;
            if ((err = usb_host_device_info(this->device_handle_, &dev_info)) != ESP_OK) {
              ESP_LOGD(TAG, "Device info failed: %s", esp_err_to_name(err));
              this->disconnect_();
              break;
            }
            ESP_LOGD(TAG, "Manuf: %s; Prod: %s; Serial: %s",
                     get_descriptor_string(dev_info.str_desc_manufacturer).c_str(),
                     get_descriptor_string(dev_info.str_desc_product).c_str(),
                     get_descriptor_string(dev_info.str_desc_serial_num).c_str());
            this->state_ = USB_CLIENT_CONNECTED;
            this->on_connected_();
          } else {
            ESP_LOGD(TAG, "Not our device, closing");
            this->disconnect_();
          }
        }
        break;
      }

      default:
        err = usb_host_client_handle_events(this->handle_, 0);
        break;
    }
  }

 protected:
  void disconnect_() {
    usb_host_device_close(this->handle_, this->device_handle_);
    this->state_ = USB_CLIENT_INIT;
    this->on_disconnected_();
  }
  virtual void on_connected_() {}
  virtual void on_disconnected_() {}
  usb_host_client_handle_t handle_{};
  usb_device_handle_t device_handle_{};
  int device_addr_{-1};
  int state_{USB_CLIENT_INIT};
  uint16_t vid_{};
  uint16_t pid_{};
  usb_transfer_t *transfer_{};
};
class USBHost : public Component {
 public:
  float get_setup_priority() const override { return setup_priority::BUS; }
  void loop() override {
    int err;
    unsigned event_flags;
    err = usb_host_lib_handle_events(0, &event_flags);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
      ESP_LOGD(TAG, "lib_handle_events failed failed: %s", esp_err_to_name(err));
    if (event_flags != 0)
      ESP_LOGD(TAG, "Event flags %X", event_flags);
  }

  void setup() override {
    ESP_LOGCONFIG(TAG, "Setup starts");
    usb_host_config_t config{};

    if (usb_host_install(&config) != ESP_OK) {
      this->status_set_error("usb_host_install failed");
      this->mark_failed();
      return;
    }
    ESP_LOGCONFIG(TAG, "Setup complete");
  }

 protected:
  std::vector<USBClient *> clients_{};
};

}  // namespace usb_host
}  // namespace esphome