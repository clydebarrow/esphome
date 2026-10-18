#include "sx126x_packet_interface.h"
#include "esphome/core/log.h"

namespace esphome::sx126x {

using packet_interface::PacketBuffer;
using packet_interface::PacketMetaData;

ESPHOME_LOG_TAG(TAG, "sx126x.packet_interface");

bool SX126xPacketInterface::send_to_interface(const PacketBuffer &data, const PacketMetaData &meta_data) {
  size_t size = data.size();
  if (size == 0)
    return true;
  if (size > this->max_packet_size_) {
    ESP_LOGW(TAG, "Packet of %zu bytes is larger than %zu", size, this->max_packet_size_);
    return false;
  }
  if (data.is_contiguous()) {
    auto span = data.as_span();
    return this->radio_->transmit_packet(span.data(), span.size()) == SX126xError::NONE;
  }
  uint8_t buf[SX126X_MAX_PACKET_SIZE];
  data.copy_to(buf, size);
  return this->radio_->transmit_packet(buf, size) == SX126xError::NONE;
}

void SX126xPacketInterface::on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) {
  PacketMetaData meta_data;
  meta_data.rssi = rssi;
  meta_data.snr = snr;
  this->on_receive_from_interface_(PacketBuffer(packet.data(), packet.size()), meta_data);
}

}  // namespace esphome::sx126x
