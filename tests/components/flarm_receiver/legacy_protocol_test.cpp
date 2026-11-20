#include <gtest/gtest.h>

#include <cstring>

#include "esphome/components/flarm_receiver/legacy_protocol.h"
#include "esphome/core/helpers.h"

namespace esphome::flarm_receiver::testing {

// An Air V7 packet from a SoftRF on the bench, received at this time
static constexpr uint8_t V7_PACKET[LEGACY_PACKET_SIZE] = {0x65, 0x72, 0x62, 0x22, 0x00, 0x00, 0x00, 0x33,
                                                          0xce, 0xc0, 0x0f, 0x25, 0x39, 0x3f, 0x5e, 0x23,
                                                          0x87, 0xa2, 0xe3, 0x38, 0x61, 0xe5, 0x02, 0x93};
static constexpr uint32_t V7_TIME = 1793909931;

static void make_frame(uint8_t frame[LEGACY_FRAME_SIZE]) { legacy_pack(V7_PACKET, frame); }

TEST(FlarmLegacy, UnpackChecksManchesterAndCrc) {
  uint8_t frame[LEGACY_FRAME_SIZE];
  make_frame(frame);
  uint8_t packet[LEGACY_PACKET_SIZE];
  ASSERT_TRUE(legacy_unpack(frame, sizeof(frame), packet));
  EXPECT_EQ(std::memcmp(packet, V7_PACKET, LEGACY_PACKET_SIZE), 0);

  frame[10] ^= 0x03;  // a chip pair that is neither 01 nor 10
  EXPECT_FALSE(legacy_unpack(frame, sizeof(frame), packet));
  make_frame(frame);
  frame[10] ^= 0x0F;  // two valid pairs swapped: a bit error the CRC catches
  EXPECT_FALSE(legacy_unpack(frame, sizeof(frame), packet));
  EXPECT_FALSE(legacy_unpack(frame, LEGACY_FRAME_SIZE - 1, packet));
}

TEST(FlarmLegacy, DecodesV7Position) {
  uint8_t packet[LEGACY_PACKET_SIZE];
  std::memcpy(packet, V7_PACKET, sizeof(packet));
  // A reference position within a degree or two is enough
  const OwnPosition own{-31.0f, 153.0f, 0.0f, V7_TIME};
  LegacyTarget target{};
  ASSERT_TRUE(legacy_decode(packet, own, target));
  EXPECT_EQ(target.address, 0x627265u);
  EXPECT_EQ(target.address_type, 2);
  EXPECT_EQ(target.aircraft_type, 1);
  EXPECT_NEAR(target.latitude, -31.42104f, 1e-4f);
  EXPECT_NEAR(target.longitude, 152.75728f, 1e-4f);
  EXPECT_NEAR(target.altitude, 39.0f, 1.0f);
  EXPECT_NEAR(target.speed, 0.0f, 0.1f);
}

TEST(FlarmLegacy, V7ToleratesClockDifference) {
  // The sender's time stamp is one second before V7_TIME; a receiver within 2 seconds of it decodes the packet
  for (int offset = -3; offset <= 1; offset++) {
    uint8_t packet[LEGACY_PACKET_SIZE];
    std::memcpy(packet, V7_PACKET, sizeof(packet));
    const OwnPosition own{-31.0f, 153.0f, 0.0f, V7_TIME + offset};
    LegacyTarget target{};
    ASSERT_TRUE(legacy_decode(packet, own, target)) << offset;
    EXPECT_NEAR(target.latitude, -31.42104f, 1e-4f) << offset;
  }
  uint8_t packet[LEGACY_PACKET_SIZE];
  std::memcpy(packet, V7_PACKET, sizeof(packet));
  LegacyTarget target{};
  EXPECT_FALSE(legacy_decode(packet, OwnPosition{-31.0f, 153.0f, 0.0f, V7_TIME + 2}, target));
}

TEST(FlarmLegacy, RejectsOtherPacketTypes) {
  uint8_t packet[LEGACY_PACKET_SIZE];
  std::memcpy(packet, V7_PACKET, sizeof(packet));
  packet[3] = (packet[3] & 0xF0) | 0x01;  // type 1, not a position
  const OwnPosition own{-31.0f, 153.0f, 0.0f, V7_TIME};
  LegacyTarget target{};
  EXPECT_FALSE(legacy_decode(packet, own, target));
}

TEST(FlarmLegacy, EncodeRoundTrip) {
  const OwnAircraft own{0x7C1FD7, 1, 1, false, -28.2f, 152.1f, 1500.0f, 30.0f, 30.0f, 270.0f, 2.5f, V7_TIME};
  uint8_t packet[LEGACY_PACKET_SIZE];
  legacy_encode(own, packet);
  uint8_t frame[LEGACY_FRAME_SIZE];
  legacy_pack(packet, frame);
  uint8_t received[LEGACY_PACKET_SIZE];
  ASSERT_TRUE(legacy_unpack(frame, sizeof(frame), received));
  LegacyTarget target{};
  // The receiver is elsewhere, with its own geoid separation
  ASSERT_TRUE(legacy_decode(received, OwnPosition{-28.0f, 152.5f, 30.0f, V7_TIME + 1}, target));
  EXPECT_EQ(target.address, 0x7C1FD7u);
  EXPECT_EQ(target.address_type, 1);
  EXPECT_EQ(target.aircraft_type, 1);
  EXPECT_NEAR(target.latitude, -28.2f, 1e-4f);
  EXPECT_NEAR(target.longitude, 152.1f, 1e-4f);
  EXPECT_NEAR(target.altitude, 1500.0f, 1.0f);
  EXPECT_NEAR(target.speed, 30.0f, 0.2f);
  EXPECT_NEAR(target.course, 270.0f, 0.5f);
  EXPECT_NEAR(target.climb_rate, 2.5f, 0.1f);
}

}  // namespace esphome::flarm_receiver::testing
