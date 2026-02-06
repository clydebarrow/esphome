#include "skyecho_simulate_switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace skyecho {

static const char *const TAG = "skyecho.switch";

void SkyEchoSimulateSwitch::setup() {
  // Initialize to off by default
  this->publish_state(false);
}

void SkyEchoSimulateSwitch::dump_config() { LOG_SWITCH("", "SkyEcho Simulate Switch", this); }

void SkyEchoSimulateSwitch::write_state(bool state) {
  if (this->parent_ != nullptr) {
    this->parent_->set_simulate(state);
    ESP_LOGI(TAG, "Simulation %s", state ? "enabled" : "disabled");
  }
  this->publish_state(state);
}

}  // namespace skyecho
}  // namespace esphome
