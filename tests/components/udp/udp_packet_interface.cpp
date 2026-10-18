#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "esphome/components/udp/packet_interface/udp_packet_interface.h"

#ifdef USE_NETWORK

namespace esphome::udp::testing {

static constexpr std::array<uint8_t, 4> PEER{192, 168, 1, 60};

class TestUdp : public UdpPacketInterface {
 public:
  explicit TestUdp(std::array<uint8_t, 4> peer) : UdpPacketInterface(nullptr, peer) {}
  using UdpPacketInterface::accepts_from_;
};

static sockaddr_in ipv4(std::array<uint8_t, 4> address) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  memcpy(&addr.sin_addr.s_addr, address.data(), address.size());
  return addr;
}

TEST(UdpPacketInterfaceTest, WithoutPeerAcceptsAnySender) {
  TestUdp link({});
  EXPECT_FALSE(link.has_capability(packet_interface::CAPABILITY_ADDRESSING));
  auto other = ipv4({10, 0, 0, 1});
  EXPECT_TRUE(link.accepts_from_(reinterpret_cast<sockaddr *>(&other)));
}

TEST(UdpPacketInterfaceTest, WithPeerAcceptsOnlyPeer) {
  TestUdp link(PEER);
  EXPECT_TRUE(link.has_capability(packet_interface::CAPABILITY_ADDRESSING));
  auto peer = ipv4(PEER);
  auto other = ipv4({192, 168, 1, 61});
  EXPECT_TRUE(link.accepts_from_(reinterpret_cast<sockaddr *>(&peer)));
  EXPECT_FALSE(link.accepts_from_(reinterpret_cast<sockaddr *>(&other)));
}

TEST(UdpPacketInterfaceTest, WithPeerDropsSenderNotIpv4) {
  TestUdp link(PEER);
  sockaddr other{};
  other.sa_family = AF_UNSPEC;
  EXPECT_FALSE(link.accepts_from_(&other));
}

}  // namespace esphome::udp::testing

#endif
