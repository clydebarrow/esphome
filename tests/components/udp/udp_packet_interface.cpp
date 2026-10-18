#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "esphome/components/udp/packet_interface/udp_packet_interface.h"

#ifdef USE_NETWORK

namespace esphome::udp::testing {

using packet_interface::PacketBuffer;
using packet_interface::PacketMetaData;
using Packet = std::vector<uint8_t>;

static constexpr std::array<uint8_t, 4> PEER{192, 168, 1, 60};

class TestUdp : public UdpPacketInterface {
 public:
  explicit TestUdp(UDPComponent *udp, std::array<uint8_t, 4> peer = {}) : UdpPacketInterface(udp, peer) {
    this->set_max_packet_size(8);
    this->add_packet_interface_listener(
        [this](const PacketBuffer &data, const PacketMetaData &) { this->received.push_back(data.to_vector()); });
  }
  using UdpPacketInterface::accepts_from_;
  using UdpPacketInterface::receive_;

  std::vector<Packet> received;
};

static sockaddr_in ipv4(std::array<uint8_t, 4> address) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  memcpy(&addr.sin_addr.s_addr, address.data(), address.size());
  return addr;
}

// The udp component is not set up, so it has no destinations and a send does nothing
class UdpPacketInterfaceTest : public ::testing::Test {
 protected:
  UDPComponent udp;
  TestUdp link{&udp};
  sockaddr_in sender{ipv4({10, 0, 0, 1})};
};

TEST_F(UdpPacketInterfaceTest, ReceivedPacketsUpToMaxPassedUp) {
  Packet packet{1, 2, 3, 4, 5, 6, 7, 8};
  this->link.receive_(packet, reinterpret_cast<sockaddr *>(&this->sender));
  Packet oversized{1, 2, 3, 4, 5, 6, 7, 8, 9};
  this->link.receive_(oversized, reinterpret_cast<sockaddr *>(&this->sender));
  EXPECT_EQ(this->link.received, std::vector<Packet>{packet});
}

TEST_F(UdpPacketInterfaceTest, SendsContiguousAndSplitPackets) {
  this->link.setup();
  Packet packet{1, 2, 3, 4};
  EXPECT_TRUE(this->link.send_to_interface(PacketBuffer(packet), {}));
  uint8_t header = 0;
  PacketBuffer body(packet);
  EXPECT_TRUE(this->link.send_to_interface(PacketBuffer(&header, 1, &body, nullptr, 0), {}));
}

TEST_F(UdpPacketInterfaceTest, OversizedPacketNotSent) {
  this->link.setup();
  Packet packet{1, 2, 3, 4, 5, 6, 7, 8, 9};
  EXPECT_FALSE(this->link.send_to_interface(PacketBuffer(packet), {}));
  uint8_t header = 0;
  Packet body_data{1, 2, 3, 4, 5, 6, 7, 8};
  PacketBuffer body(body_data);
  EXPECT_FALSE(this->link.send_to_interface(PacketBuffer(&header, 1, &body, nullptr, 0), {}));
}

TEST_F(UdpPacketInterfaceTest, WithoutPeerAcceptsAnySender) {
  EXPECT_FALSE(this->link.has_capability(packet_interface::CAPABILITY_ADDRESSING));
  EXPECT_TRUE(this->link.accepts_from_(reinterpret_cast<sockaddr *>(&this->sender)));
}

TEST_F(UdpPacketInterfaceTest, WithPeerAcceptsOnlyPeer) {
  TestUdp link(&this->udp, PEER);
  EXPECT_TRUE(link.has_capability(packet_interface::CAPABILITY_ADDRESSING));
  auto peer = ipv4(PEER);
  Packet packet{1, 2};
  link.receive_(packet, reinterpret_cast<sockaddr *>(&this->sender));
  EXPECT_TRUE(link.received.empty());
  link.receive_(packet, reinterpret_cast<sockaddr *>(&peer));
  EXPECT_EQ(link.received, std::vector<Packet>{packet});
}

TEST_F(UdpPacketInterfaceTest, WithPeerDropsSenderNotIpv4) {
  TestUdp link(&this->udp, PEER);
  sockaddr other{};
  other.sa_family = AF_UNSPEC;
  EXPECT_FALSE(link.accepts_from_(&other));
}

}  // namespace esphome::udp::testing

#endif
