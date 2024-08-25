#include "usb_host.h"
#include "usb/usb_host.h"
#include "esphome/core/log.h"

namespace esphome {
namespace usb_host {
void USBHost::setup() {
  ESP_LOGCONFIG(TAG, "Setup starts");
  usb_host_config_t config{};

  if (usb_host_install(&config) != ESP_OK) {
    this->status_set_error("usb_host_install failed");
    this->mark_failed();
    return;
  }
  ESP_LOGCONFIG(TAG, "Setup complete");
}
void USBHost::loop() {
  int err;
  unsigned event_flags;
  err = usb_host_lib_handle_events(0, &event_flags);
  if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
    ESP_LOGD(TAG, "lib_handle_events failed failed: %s", esp_err_to_name(err));
  if (event_flags != 0)
    ESP_LOGD(TAG, "Event flags %X", event_flags);
}

}  // namespace usb_host
}  // namespace esphome