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

static void manchester_encode(const uint8_t *bytes, size_t len, uint8_t *frame) {
  for (size_t i = 0; i != len; i++) {
    uint16_t chips = 0;
    for (unsigned bit = 0; bit != 8; bit++)
      chips |= ((bytes[i] & (0x80 >> bit)) ? 1 : 2) << (14 - bit * 2);
    frame[i * 2] = chips >> 8;
    frame[i * 2 + 1] = chips & 0xFF;
  }
}

static void make_frame(uint8_t frame[LEGACY_FRAME_SIZE]) {
  static constexpr uint8_t ADDRESS[3] = {0x31, 0xFA, 0xB6};
  uint8_t bytes[LEGACY_PACKET_SIZE + 2];
  std::memcpy(bytes, V7_PACKET, LEGACY_PACKET_SIZE);
  uint16_t crc = crc16be(ADDRESS, sizeof(ADDRESS), 0xFFFF);
  crc = crc16be(V7_PACKET, LEGACY_PACKET_SIZE, crc);
  bytes[LEGACY_PACKET_SIZE] = crc >> 8;
  bytes[LEGACY_PACKET_SIZE + 1] = crc & 0xFF;
  manchester_encode(bytes, sizeof(bytes), frame);
}

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

}  // namespace esphome::flarm_receiver::testing
