#pragma once

#include "esphome/core/defines.h"

// udp_component.h declares nothing without USE_NETWORK
#ifdef USE_NETWORK
#include "esphome/components/packet_interface/packet_interface.h"
#include "../udp_component.h"
#include <array>

namespace esphome::udp {

/**
 * A packet interface link over UDP.
 *
 * When the UDP component sends to one unicast IPv4 address, the link joins exactly two devices: it accepts packets
 * only from that address, and provides the addressing capability itself. Otherwise every packet is accepted, and an
 * addressing layer can tell the devices apart.
 */
class UdpPacketInterface : public packet_interface::PacketInterface {
 public:
  /// @param peer The IPv4 address of the one peer, in network byte order; all zeros if packets are broadcast
  UdpPacketInterface(UDPComponent *udp_component, std::array<uint8_t, 4> peer);

  void setup() override;
  bool send_to_interface(const packet_interface::PacketBuffer &data,
                         const packet_interface::PacketMetaData &meta_data) override;

 protected:
  UDPComponent *parent_;
  std::array<uint8_t, 4> peer_;
};
}  // namespace esphome::udp
#endif
