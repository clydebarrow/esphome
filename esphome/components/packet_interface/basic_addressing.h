#pragma once

#include "esphome/core/defines.h"
#ifdef USE_PACKET_INTERFACE_BASIC_ADDRESSING
#include "packet_interface.h"

namespace esphome::packet_interface {

/**
 * An addressing layer for channels shared by several devices, such as an RS485 bus or a LoRa radio.
 *
 * Each packet starts with a 16-bit destination address and a 16-bit source address, little endian.
 * BROADCAST_ADDRESS sends to every device. A packet is dropped unless it is addressed to this device or broadcast, and
 * a packet from this device's own address, such as an echo on a half-duplex bus, is dropped too.
 *
 * With a destination other than broadcast, the layer is a point-to-point link: it sends to that device and accepts
 * packets only from it. The reliability and session layers, and the rolling code, need this, as they work between
 * two devices only.
 *
 * Received packets report their source and destination addresses in the metadata. A destination address in the
 * metadata of a packet being sent overrides the configured destination.
 */
class BasicAddressing : public PacketInterface {
 public:
  static constexpr uint16_t BROADCAST_ADDRESS = 0xFFFF;
  /// Destination and source addresses.
  static constexpr size_t HEADER_SIZE = 4;

  BasicAddressing(PacketInterface *lower, uint16_t address, uint16_t destination);

  void setup() override;
  void dump_config() override;
  bool send_to_interface(const PacketBuffer &data, const PacketMetaData &meta_data) override;

 protected:
  void receive_(const PacketBuffer &data, const PacketMetaData &meta_data);

  PacketInterface *lower_;
  const uint16_t address_;
  const uint16_t destination_;
};

}  // namespace esphome::packet_interface
#endif
