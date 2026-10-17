#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <vector>
#include "esphome/components/packet_interface/basic_addressing.h"

namespace esphome::packet_interface::addressing_testing {

using Packet = std::vector<uint8_t>;

static constexpr uint16_t BROADCAST = BasicAddressing::BROADCAST_ADDRESS;

// A link layer that records what is sent, so a test can deliver it to another device
class MockLink : public PacketInterface {
 public:
  MockLink() { this->set_max_packet_size(64); }
  bool send_to_interface(const PacketBuffer &data, const PacketMetaData &) override {
    this->sent.push_back(data.to_vector());
    return true;
  }
  void inject(const Packet &packet) { this->on_receive_from_interface_(PacketBuffer(packet), {}); }

  std::vector<Packet> sent;
};

struct Device {
  Device(uint16_t address, uint16_t destination = BROADCAST) {
    this->addressing = std::make_unique<BasicAddressing>(&this->link, address, destination);
    this->addressing->add_packet_interface_listener([this](const PacketBuffer &data, const PacketMetaData &meta_data) {
      this->received.push_back(data.to_vector());
      this->sources.push_back(meta_data.source_address);
    });
    this->addressing->setup();
  }
  void send(const Packet &data, uint16_t destination = 0) {
    PacketMetaData meta_data;
    meta_data.destination_address = destination;
    this->addressing->send_to_interface(PacketBuffer(data), meta_data);
  }
  // Every device on the bus hears every packet, including its own
  static void deliver(Device &from, std::initializer_list<Device *> bus) {
    for (auto &packet : from.link.sent) {
      for (auto *device : bus)
        device->link.inject(packet);
    }
    from.link.sent.clear();
  }

  MockLink link;
  std::unique_ptr<BasicAddressing> addressing;
  std::vector<Packet> received;
  std::vector<uint16_t> sources;
};

TEST(BasicAddressingTest, HeaderAndCapability) {
  Device a(0x1234, 0x5678);
  EXPECT_EQ(a.addressing->get_max_packet_size(), 64 - BasicAddressing::HEADER_SIZE);
  EXPECT_TRUE(a.addressing->has_capability(CAPABILITY_ADDRESSING));
  a.send({0xAA});
  a.send({0xBB}, 0x0042);
  EXPECT_EQ(a.link.sent, (std::vector<Packet>{{0x78, 0x56, 0x34, 0x12, 0xAA}, {0x42, 0x00, 0x34, 0x12, 0xBB}}));
}

TEST(BasicAddressingTest, BroadcastReachesEveryoneButSender) {
  Device a(1), b(2), c(3);
  a.send({0x10});
  Device::deliver(a, {&a, &b, &c});
  EXPECT_TRUE(a.received.empty());
  EXPECT_EQ(b.received, std::vector<Packet>{{0x10}});
  EXPECT_EQ(c.received, std::vector<Packet>{{0x10}});
  EXPECT_EQ(b.sources, std::vector<uint16_t>{1});
}

TEST(BasicAddressingTest, UnicastReachesOnlyDestination) {
  Device a(1, 2), b(2), c(3);
  a.send({0x20});
  Device::deliver(a, {&b, &c});
  EXPECT_EQ(b.received, std::vector<Packet>{{0x20}});
  EXPECT_TRUE(c.received.empty());
}

TEST(BasicAddressingTest, PointToPointAcceptsOnlyPeer) {
  Device a(1, 2), b(2, 1), c(3);
  c.send({0x30});     // broadcast from a third device
  c.send({0x31}, 2);  // addressed to b, from the wrong device
  a.send({0x32});     // from the peer
  Device::deliver(c, {&b});
  Device::deliver(a, {&b});
  EXPECT_EQ(b.received, std::vector<Packet>{{0x32}});
  EXPECT_EQ(b.sources, std::vector<uint16_t>{1});
}

TEST(BasicAddressingTest, ShortPacketDropped) {
  Device a(1);
  a.link.inject({0xFF, 0xFF, 0x02});
  EXPECT_TRUE(a.received.empty());
}

}  // namespace esphome::packet_interface::addressing_testing
