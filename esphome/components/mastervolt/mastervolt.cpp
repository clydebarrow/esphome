#include "mastervolt.h"

namespace esphome::mastervolt {

using namespace bytebuffer;
void Mastervolt::on_receive_(uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data) {
  char hexbuf[format_hex_pretty_size(32)];
  if (!extended_id)
    return;
  for (auto *device : this->devices_) {
    if (device->can_id_ == can_id) {
      if (this->debug_)
        ESP_LOGD(TAG, "Known message - id: 0x%08" PRIx32 ", data: %s", can_id, format_hex_pretty_to(hexbuf, data));
      device->on_receive(ByteBuffer::wrap(data, LITTLE));
      return;
    }
  }
  if (this->debug_)
    ESP_LOGD(TAG, "unknown message - id: 0x%08" PRIx32 ", data: %s", can_id, format_hex_pretty_to(hexbuf, data));
}

void Mastervolt::setup() {
  this->canbus_->add_callback([this](uint32_t can_id, bool extended_id, bool rtr, const std::vector<uint8_t> &data) {
    this->on_receive_(can_id, extended_id, rtr, data);
  });
}
void MastervoltDevice::on_receive(ByteBuffer data) {
  char hexbuf[format_hex_pretty_size(32)];
  ESP_LOGV(TAG, "data received: %s", format_hex_pretty_to(hexbuf, data.get_data()));
  auto command = data.get_uint16(0);
  if (this->sensors_.contains(command)) {
    this->sensors_[command]->process_message(data);
    return;
  }
  ESP_LOGD(TAG, "Unknown data %s for device 0x%08" PRIx32, format_hex_pretty_to(hexbuf, data.get_data()),
           this->can_id_);
}

}  // namespace esphome::mastervolt
