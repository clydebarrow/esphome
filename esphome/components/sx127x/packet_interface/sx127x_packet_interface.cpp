#include "sx127x_packet_interface.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::sx127x {

using packet_interface::PacketBuffer;
using packet_interface::PacketMetaData;

ESPHOME_LOG_TAG(TAG, "sx127x.packet_interface");

// Longer than any packet takes to receive, so a radio flag that never clears cannot stop sending for good
static constexpr uint32_t MAX_BUSY_MS = 2000;
// A refusal this long after the previous one starts a new busy period
static constexpr uint32_t BUSY_GAP_MS = 500;

bool SX127xPacketInterface::send_to_interface(const PacketBuffer &data, const PacketMetaData &meta_data) {
  size_t size = data.size();
  if (size == 0)
    return true;
  if (size > this->max_packet_size_) {
    ESP_LOGW(TAG, "Packet of %zu bytes is larger than %zu", size, this->max_packet_size_);
    return false;
  }
  if (this->radio_->is_receiving()) {
    uint32_t now = millis();
    if (now - this->last_refused_ms_ > BUSY_GAP_MS)
      this->busy_since_ms_ = now;
    this->last_refused_ms_ = now;
    if (now - this->busy_since_ms_ < MAX_BUSY_MS)
      return false;
    ESP_LOGV(TAG, "Radio busy for %" PRIu32 "ms, sending anyway", now - this->busy_since_ms_);
  }
  if (data.is_contiguous()) {
    auto span = data.as_span();
    return this->radio_->transmit_packet(span.data(), span.size()) == SX127xError::NONE;
  }
  uint8_t buf[SX127X_MAX_PACKET_SIZE];
  data.copy_to(buf, size);
  return this->radio_->transmit_packet(buf, size) == SX127xError::NONE;
}

void SX127xPacketInterface::on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) {
  PacketMetaData meta_data;
  meta_data.rssi = rssi;
  meta_data.snr = snr;
  this->on_receive_from_interface_(PacketBuffer(packet.data(), packet.size()), meta_data);
}

}  // namespace esphome::sx127x
