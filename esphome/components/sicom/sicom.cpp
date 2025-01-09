#include "esphome/core/log.h"
#include "sicom.h"

namespace esphome {
namespace sicom {

static const char *TAG = "sicom";

void SicomComponent::setup() {

}

void SicomComponent::loop() {

}

void SicomComponent::dump_config(){
  ESP_LOGCONFIG(TAG, "Sicom");
}

}  // namespace sicom
}  // namespace esphome
