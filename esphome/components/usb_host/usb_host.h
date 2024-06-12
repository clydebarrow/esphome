#pragma once

#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "usb/usb_host.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <map>
#include <vector>

namespace esphome {
namespace usb_host {

static const char *TAG = "usb_host";

enum ClientState {
  USB_CLIENT_INIT = 0,
  USB_CLIENT_OPEN,
  USB_CLIENT_CLOSE,
  USB_CLIENT_GET_DESC,
  USB_CLIENT_CONNECTED,
};
class USBClient {
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
  static void client_task(void *ptr) {
    auto *client = (USBClient *) ptr;

    ESP_LOGD(TAG, "Client task starts");
    usb_host_client_config_t config{.is_synchronous = false,
                                    .max_num_event_msg = 5,
                                    .async = {.client_event_callback = client_event_cb, .callback_arg = client}};
    auto err = usb_host_client_register(&config, &client->handle_);
    if (err != ESP_OK) {
      ESP_LOGD(TAG, "client register failed: %s", esp_err_to_name(err));
      return;
    }
    do {
      switch (client->state_) {
        case USB_CLIENT_OPEN: {
          ESP_LOGD(TAG, "Open device %d", client->device_addr_);
          if ((err = usb_host_device_open(client->handle_, client->device_addr_, &client->device_handle_)) != ESP_OK) {
            ESP_LOGD(TAG, "Device open failed: %s", esp_err_to_name(err));
            client->state_ = USB_CLIENT_INIT;
            break;
          }
          ESP_LOGD(TAG, "Get descriptor device %d", client->device_addr_);
          const usb_device_desc_t *desc;
          if ((err = usb_host_get_device_descriptor(client->device_handle_, &desc)) != ESP_OK) {
            ESP_LOGD(TAG, "Device get_desc failed: %s", esp_err_to_name(err));
          } else {
            ESP_LOGD(TAG, "Device descriptor: vid %X pid %X", desc->idVendor, desc->idProduct);
            client->state_ = USB_CLIENT_CONNECTED;
          }
          break;
        }
        default:
          err = usb_host_client_handle_events(client->handle_, 1);
          break;
      }
    } while (err == ESP_OK);
  }

 public:
  void setup() {
    // xTaskCreate(client_task, "USBClient", 4096, this, 1, &this->task_handle_);
    usb_host_client_config_t config{.is_synchronous = false,
                                    .max_num_event_msg = 5,
                                    .async = {.client_event_callback = client_event_cb, .callback_arg = this}};
    auto err = usb_host_client_register(&config, &this->handle_);
    if (err != ESP_OK) {
      ESP_LOGD(TAG, "client register failed: %s", esp_err_to_name(err));
      return;
    }
  }

  void loop() {
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
        } else {
          ESP_LOGD(TAG, "Device descriptor: vid %X pid %X", desc->idVendor, desc->idProduct);
          this->state_ = USB_CLIENT_CONNECTED;
        }
        break;
      }
      default:
        err = usb_host_client_handle_events(this->handle_, 0);
        break;
    }
  }

 protected:
  TaskHandle_t task_handle_{};
  usb_host_client_handle_t handle_{};
  usb_device_handle_t device_handle_{};
  int device_addr_{-1};
  int state_{USB_CLIENT_INIT};
};
class USBHost : public Component {
  static void daemon_task(void *ptr) {
    USBHost *host = (USBHost *) ptr;

    esp_err_t err;

    unsigned event_flags;
    ESP_LOGD(TAG, "Daemon task starts");
    host->client_.setup();
    while ((err = usb_host_lib_handle_events(0, &event_flags)) == ESP_OK) {
      ESP_LOGD(TAG, "Event flags %X", event_flags);
    }
    ESP_LOGE(TAG, "host_lib_handle_events error %s", esp_err_to_name(err));
  }

 public:
  void loop() override {
    int err;
    unsigned event_flags;
    err = usb_host_lib_handle_events(0, &event_flags);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
      ESP_LOGD(TAG, "lib_handle_events failed failed: %s", esp_err_to_name(err));
    if (event_flags != 0)
      ESP_LOGD(TAG, "Event flags %X", event_flags);
    this->client_.loop();
  }

  void setup() override {
    ESP_LOGCONFIG(TAG, "Setup starts");
    usb_host_config_t config{};

    if (usb_host_install(&config) != ESP_OK) {
      this->status_set_error("usb_host_install failed");
      this->mark_failed();
      return;
    }
    this->client_.setup();
    /*int err = xTaskCreate(daemon_task, "usbDaemon", 4096, this, 1, &this->daemon_task_handle_);
    if (err != pdPASS) {
      ESP_LOGE(TAG, "daemon task start failed: %d", err);
      this->status_set_error("daemon task start failed");
      this->mark_failed();
      return;
    } */
    ESP_LOGCONFIG(TAG, "Setup complete");
  }

 protected:
  TaskHandle_t daemon_task_handle_{};
  std::vector<USBClient *> clients_{};
};

}  // namespace usb_host
}  // namespace esphome