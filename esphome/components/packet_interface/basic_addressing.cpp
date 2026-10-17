#include "basic_addressing.h"
#ifdef USE_PACKET_INTERFACE_BASIC_ADDRESSING
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::packet_interface {

ESPHOME_LOG_TAG(TAG, "packet_interface.addressing");

BasicAddressing::BasicAddressing(PacketInterface *lower, uint16_t address, uint16_t destination)
    : lower_(lower), address_(address), destination_(destination) {
  this->stack_on_(lower, CAPABILITY_ADDRESSING);
  this->max_packet_size_ = lower->get_max_packet_size() - HEADER_SIZE;
}

void BasicAddressing::setup() {
  this->lower_->add_packet_interface_listener(
      [this](const PacketBuffer &data, const PacketMetaData &meta_data) { this->receive_(data, meta_data); });
}

void BasicAddressing::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Basic Addressing on '%s':\n"
                "  Max Packet Size: %zu\n"
                "  Address: 0x%04X",
                this->name_, this->max_packet_size_, this->address_);
  if (this->destination_ == BROADCAST_ADDRESS) {
    ESP_LOGCONFIG(TAG, "  Destination: broadcast");
  } else {
    ESP_LOGCONFIG(TAG, "  Destination: 0x%04X", this->destination_);
  }
}

bool BasicAddressing::send_to_interface(const PacketBuffer &data, const PacketMetaData &meta_data) {
  uint16_t destination = meta_data.destination_address != 0 ? meta_data.destination_address : this->destination_;
  uint8_t header[HEADER_SIZE] = {static_cast<uint8_t>(destination), static_cast<uint8_t>(destination >> 8),
                                 static_cast<uint8_t>(this->address_), static_cast<uint8_t>(this->address_ >> 8)};
  PacketBuffer packet(header, HEADER_SIZE, &data, nullptr, 0);
  return this->lower_->send_to_interface(packet, meta_data);
}

void BasicAddressing::receive_(const PacketBuffer &data, const PacketMetaData &meta_data) {
  // Lower layers deliver packets in one piece; the payload is passed up without copying
  if (!data.is_contiguous()) {
    ESP_LOGW(TAG, "%s: Packet split across buffers is not supported", this->name_);
    return;
  }
  auto packet = data.as_span();
  if (packet.size() < HEADER_SIZE)
    return;
  uint16_t destination = encode_uint16(packet[1], packet[0]);
  uint16_t source = encode_uint16(packet[3], packet[2]);
  if (source == this->address_ || (destination != this->address_ && destination != BROADCAST_ADDRESS))
    return;
  if (this->destination_ != BROADCAST_ADDRESS && source != this->destination_) {
    ESP_LOGV(TAG, "%s: Packet from 0x%04X dropped", this->name_, source);
    return;
  }
  PacketMetaData received = meta_data;
  received.source_address = source;
  received.destination_address = destination;
  this->on_receive_from_interface_(PacketBuffer(packet.data() + HEADER_SIZE, packet.size() - HEADER_SIZE), received);
}

}  // namespace esphome::packet_interface
#endif
