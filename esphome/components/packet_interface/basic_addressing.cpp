#include "basic_addressing.h"
#ifdef USE_PACKET_INTERFACE_BASIC_ADDRESSING
#include <memory>
#include "esphome/core/log.h"
#ifdef USE_PACKET_INTERFACE_XXTEA
#include "xxtea_encryption.h"
#endif

namespace esphome::packet_interface {

ESPHOME_LOG_TAG(TAG, "packet_interface.addressing");

void BasicAddressing::setup() {
  this->link_->add_packet_interface_listener(
      [this](const PacketBuffer &data, const PacketMetaData &meta_data) { this->receive_(data, meta_data); });
}

void BasicAddressing::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Basic Addressing on '%s':\n"
                "  Address: 0x%04X",
                this->link_->get_name(), this->address_);
}

bool BasicAddressing::send(const PacketBuffer &data, const PacketMetaData &meta_data, uint16_t destination) {
  uint16_t chosen = meta_data.destination_address;
  if (chosen != 0 && chosen != destination) {
    if (destination != BROADCAST_ADDRESS || chosen == this->address_) {
      // The same call is made for every packet, so once is enough
      if (!this->destination_warned_) {
        this->destination_warned_ = true;
        ESP_LOGW(TAG,
                 "%s: Packet for 0x%04X refused; only the broadcast port sends to a chosen address, and not to "
                 "this device's own",
                 this->link_->get_name(), chosen);
      }
      return false;
    }
    destination = chosen;
  }
  uint8_t header[HEADER_SIZE] = {static_cast<uint8_t>(destination), static_cast<uint8_t>(destination >> 8),
                                 static_cast<uint8_t>(this->address_), static_cast<uint8_t>(this->address_ >> 8)};
  PacketBuffer packet(header, HEADER_SIZE, &data, nullptr, 0);
  return this->link_->send_to_interface(packet, meta_data);
}

void BasicAddressing::receive_(const PacketBuffer &data, const PacketMetaData &meta_data) {
  // Lower layers deliver packets in one piece; the payload is passed up without copying
  if (!data.is_contiguous()) {
    ESP_LOGW(TAG, "%s: Packet split across buffers is not supported", this->link_->get_name());
    return;
  }
  auto packet = data.as_span();
  if (packet.size() < HEADER_SIZE) {
    ESP_LOGV(TAG, "%s: Packet of %zu bytes is shorter than the address header, dropped", this->link_->get_name(),
             packet.size());
    return;
  }
  uint16_t destination = encode_uint16(packet[1], packet[0]);
  uint16_t source = encode_uint16(packet[3], packet[2]);
  if (source == this->address_ || (destination != this->address_ && destination != BROADCAST_ADDRESS))
    return;
  AddressingPort *source_port = nullptr;
  AddressingPort *broadcast_port = nullptr;
  for (auto *port : this->ports_) {
    if (port->destination_ == NO_ADDRESS)
      continue;
    if (port->destination_ == source) {
      source_port = port;
    } else if (port->destination_ == BROADCAST_ADDRESS) {
      broadcast_port = port;
    }
  }
  // A broadcast goes to the broadcast port, even from a device with a port of its own, as that port's layers expect
  // only its point-to-point packets; anything else goes to the source's port, or to the broadcast port if it has none
  AddressingPort *target = source_port;
  if (broadcast_port != nullptr && (destination == BROADCAST_ADDRESS || source_port == nullptr))
    target = broadcast_port;
  if (target == nullptr) {
    ESP_LOGV(TAG, "%s: Packet from 0x%04X dropped", this->link_->get_name(), source);
    return;
  }
  PacketMetaData received = meta_data;
  received.source_address = source;
  received.destination_address = destination;
  target->on_receive_from_interface_(PacketBuffer(packet.data() + HEADER_SIZE, packet.size() - HEADER_SIZE), received);
}

AddressingPort::AddressingPort(BasicAddressing *addressing, uint16_t destination, bool only_port)
    : PacketInterface(
          remaining_packet_size(addressing->get_link()->get_max_packet_size(), BasicAddressing::HEADER_SIZE)),
      addressing_(addressing),
      destination_(destination) {
  if (only_port) {
    this->stack_on_(addressing->get_link(), CAPABILITY_ADDRESSING);
  } else {
    this->inherit_from_(addressing->get_link(), CAPABILITY_ADDRESSING);
  }
  // A link with no room leaves the layers above none either, so they fail their setup
  addressing->add_port(this);
}

void AddressingPort::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Addressing Port '%s':\n"
                "  Max Packet Size: %zu",
                this->name_, this->max_packet_size_);
  if (this->destination_ == BasicAddressing::BROADCAST_ADDRESS) {
    ESP_LOGCONFIG(TAG, "  Destination: broadcast");
  } else if (this->destination_ == BasicAddressing::NO_ADDRESS) {
    ESP_LOGCONFIG(TAG, "  Destination: none yet");
  } else {
    ESP_LOGCONFIG(TAG, "  Destination: 0x%04X", this->destination_);
  }
}

void PeerPool::add_slot(AddressingPort *port, XxteaEncryption *encryption) {
  port->set_destination(BasicAddressing::NO_ADDRESS);
  this->slots_.push_back({port, encryption});
}

int PeerPool::find(uint16_t address) const {
  return address == BasicAddressing::NO_ADDRESS ? -1 : this->find_slot_(address);
}

int PeerPool::assign(uint16_t address, const uint8_t *key) {
  if (address == BasicAddressing::NO_ADDRESS || address == BasicAddressing::BROADCAST_ADDRESS) {
    ESP_LOGW(TAG, "%s: 0x%04X is not a device address", this->name_, address);
    return -1;
  }
  int slot = this->find(address);
  if (slot < 0)
    slot = this->find_free_();
  if (slot < 0) {
    ESP_LOGW(TAG, "%s: All %zu slots are in use, so 0x%04X has none", this->name_, this->slots_.size(), address);
    return -1;
  }
  auto &entry = this->slots_[slot];
  if (entry.encryption != nullptr) {
#ifdef USE_PACKET_INTERFACE_XXTEA
    if (key == nullptr) {
      ESP_LOGW(TAG, "%s: 0x%04X needs a key", this->name_, address);
      return -1;
    }
    entry.encryption->set_key(key);
#endif
  }
  entry.port->set_destination(address);
  return slot;
}

int PeerPool::find_free_() const { return this->find_slot_(BasicAddressing::NO_ADDRESS); }

int PeerPool::find_slot_(uint16_t destination) const {
  for (size_t i = 0; i != this->slots_.size(); i++) {
    if (this->slots_[i].port->get_destination() == destination)
      return static_cast<int>(i);
  }
  return -1;
}

void PeerPool::release(uint16_t address) {
  int slot = this->find(address);
  if (slot >= 0)
    this->free_(this->slots_[slot]);
}

void PeerPool::clear() {
  for (auto &slot : this->slots_)
    this->free_(slot);
}

void PeerPool::free_(Slot &slot) {
  slot.port->set_destination(BasicAddressing::NO_ADDRESS);
#ifdef USE_PACKET_INTERFACE_XXTEA
  if (slot.encryption != nullptr)
    slot.encryption->clear_key();
#endif
}

void PeerPool::add_listener(Listener listener) {
  // Shared, so each slot's listener holds a pointer rather than a copy
  auto shared = std::make_shared<Listener>(std::move(listener));
  for (size_t i = 0; i != this->slots_.size(); i++) {
    this->slots_[i].port->top()->add_packet_interface_listener(
        [shared, i](const PacketBuffer &data, const PacketMetaData &meta_data) { (*shared)(i, data, meta_data); });
  }
}

}  // namespace esphome::packet_interface
#endif
