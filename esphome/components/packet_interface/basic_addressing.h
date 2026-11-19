#pragma once

#include "esphome/core/defines.h"
#ifdef USE_PACKET_INTERFACE_BASIC_ADDRESSING
#include <functional>
#include <vector>
#include "esphome/core/helpers.h"
#include "packet_interface.h"

namespace esphome::packet_interface {

class AddressingPort;
class XxteaEncryption;

/**
 * An addressing layer for channels shared by several devices, such as an RS485 bus or a LoRa radio.
 *
 * Each packet starts with a 16-bit destination address and a 16-bit source address, little endian.
 * BROADCAST_ADDRESS sends to every device. A packet is dropped unless it is addressed to this device or broadcast, and
 * a packet from this device's own address, such as an echo on a half-duplex bus, is dropped too.
 *
 * Above this layer are one or more ports, each the bottom of its own stack of layers. A port with a destination
 * address is a point-to-point link to that device: it sends to it and receives only its packets, so the reliability
 * and session layers and the rolling code, which work between two devices only, can be used above it. A port with
 * the broadcast destination sends to every device, and receives every broadcast, as well as packets from devices
 * that have no port of their own. Without a broadcast port, a device's broadcasts go to its own port.
 *
 * A port can also be given its destination at run time, as the ports of a PeerPool are; until then it has none, and
 * receives nothing.
 */
class BasicAddressing : public Component {
 public:
  static constexpr uint16_t BROADCAST_ADDRESS = 0xFFFF;
  /// The destination of a port that has none yet; no device has this address
  static constexpr uint16_t NO_ADDRESS = 0;
  /// Destination and source addresses.
  static constexpr size_t HEADER_SIZE = 4;

  BasicAddressing(PacketInterface *link, uint16_t address) : link_(link), address_(address) {}

  void setup() override;
  void dump_config() override;

  PacketInterface *get_link() const { return this->link_; }
  uint16_t get_address() const { return this->address_; }
  /// Changes this device's address, as when it is read from a file at start-up rather than set in the configuration
  void set_address(uint16_t address) { this->address_ = address; }
  void add_port(AddressingPort *port) { this->ports_.push_back(port); }
  /**
   * Sends \p data to \p destination. Through the broadcast port, a destination set in \p meta_data is used instead;
   * any other port refuses one that differs from its own, as its layers may keep state for that one peer, and a
   * packet to this device's own address is refused, as every receiver drops it.
   */
  bool send(const PacketBuffer &data, const PacketMetaData &meta_data, uint16_t destination);

 protected:
  void receive_(const PacketBuffer &data, const PacketMetaData &meta_data);

  PacketInterface *link_;
  uint16_t address_;
  bool destination_warned_{false};
  StaticVector<AddressingPort *, PACKET_INTERFACE_ADDRESSING_PORT_COUNT> ports_;
};

/// The bottom of the stack of layers for one destination, or for broadcast, on an addressed channel.
class AddressingPort : public PacketInterface {
 public:
  /// With \p only_port, the port is the top of the link, so top() on the link reaches this port's stack.
  AddressingPort(BasicAddressing *addressing, uint16_t destination, bool only_port);

  void dump_config() override;
  bool send_to_interface(const PacketBuffer &data, const PacketMetaData &meta_data) override {
    return this->addressing_->send(data, meta_data, this->destination_);
  }
  uint16_t get_destination() const { return this->destination_; }
  /// Changes the device this port is for; BasicAddressing::NO_ADDRESS leaves it with none
  void set_destination(uint16_t destination) { this->destination_ = destination; }

 protected:
  friend class BasicAddressing;

  BasicAddressing *addressing_;
  uint16_t destination_;
};

/**
 * A fixed number of ports with the same layers, each given a device's address, and its key if the stack is encrypted,
 * at run time. A receiver that learns its peers from a server uses one, so a new device needs no new build. Code
 * generation makes every port and its layers, so nothing is allocated when a slot is assigned.
 *
 * Applications register one listener for the whole pool; it is told which slot a packet came in on.
 */
class PeerPool {
 public:
  using Listener = std::function<void(size_t slot, const PacketBuffer &data, const PacketMetaData &meta_data)>;

  explicit PeerPool(const char *name) : name_(name) {}

  /// Code generation adds each slot's port, and its encryption layer or null
  void add_slot(AddressingPort *port, XxteaEncryption *encryption);

  size_t size() const { return this->slots_.size(); }
  /**
   * Gives a slot to the device at \p address, with \p key (32 bytes) for an encrypted stack, and returns it. A device
   * that already has a slot keeps it, with the new key. Returns -1 if every slot is in use, or the stack is
   * encrypted and no key is given.
   */
  int assign(uint16_t address, const uint8_t *key);
  /// Frees the slot of the device at \p address, if it has one
  void release(uint16_t address);
  /// Frees every slot
  void clear();
  /// The slot of the device at \p address, or -1
  int find(uint16_t address) const;
  /// The address the slot is for, or BasicAddressing::NO_ADDRESS
  uint16_t address(size_t slot) const { return this->slots_[slot].port->get_destination(); }
  /// The top of the slot's stack, to send through
  PacketInterface *top(size_t slot) const { return this->slots_[slot].port->top(); }
  /// Calls \p listener with every packet received in any slot
  void add_listener(Listener listener);

 protected:
  struct Slot {
    AddressingPort *port;
    XxteaEncryption *encryption;
  };
  int find_free_() const;
  int find_slot_(uint16_t destination) const;
  void free_(Slot &slot);

  const char *name_;
  std::vector<Slot> slots_;
};

}  // namespace esphome::packet_interface
#endif
