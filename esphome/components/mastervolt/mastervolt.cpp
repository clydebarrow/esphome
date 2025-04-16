#include "mastervolt.h"

namespace esphome {
namespace mastervolt {

using namespace bytebuffer;
void Mastervolt::on_receive_(uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data) {
  if (!extended_id)
    return;
  for (auto *device : this->devices_) {
    if (device->can_id_ == can_id) {
      device->on_receive(ByteBuffer::wrap(data, LITTLE));
      return;
    }
  }
  if (this->debug_)
    ESP_LOGD(TAG, "unknown message - id: 0x%08" PRIx32 ", data: %s", can_id, format_hex_pretty(data).c_str());
}

void Mastervolt::setup() {
  this->canbus_->add_callback([this](uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data) {
    this->on_receive_(can_id, extended_id, rtr, data);
  });
}
void MastervoltDevice::on_receive(ByteBuffer data) {
  ESP_LOGV(TAG, "data received: %s", format_hex_pretty(data.get_data()).c_str());
  auto command = data.get_uint16(0);
  if (this->sensors_.find(command) != this->sensors_.end()) {
    this->sensors_[command]->process_message(data);
    return;
  }
  ESP_LOGD(TAG, "Unknown data %s for device 0x%08" PRIx32, format_hex_pretty(data.get_data()).c_str(), this->can_id_);
}

}  // namespace mastervolt
}  // namespace esphome
