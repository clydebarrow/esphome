#include "skyecho.h"
#include "text_sensor/skyecho_text_sensor.h"

namespace esphome {
namespace skyecho {

void SkyEcho::dump_config() {
  ESP_LOGCONFIG(TAG, "SkyEcho:");
  ESP_LOGCONFIG(TAG, "  Port: %d", PORT);
}

void SkyEcho::update() {
  // Trigger update on all registered text sensors
  for (auto *sensor : this->text_sensors_) {
    sensor->update();
  }
}

}  // namespace skyecho
}  // namespace esphome
