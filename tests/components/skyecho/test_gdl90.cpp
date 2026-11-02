#include <gtest/gtest.h>

#include "esphome/components/skyecho/gdl90.h"

namespace esphome::skyecho::testing {

// An ownship report: 1000 ft (altitude code 0x050), 60 kt
static void ownship_block(uint8_t *block) {
  memset(block, 0, 28);
  block[0] = GDL90_OWNSHIP_REPORT;
  block[11] = 0x05;
  block[12] = 0x09;
  block[14] = 0x03;
  block[15] = 0xC0;
}

TEST(SkyEchoGdl90, ReportsAnAvailableAltitudeAndSpeed) {
  uint8_t block[28];
  ownship_block(block);
  gdl90Data_t out{};
  ASSERT_EQ(gdl90DecodeBlock(block, sizeof(block), &out), GDL90_ERR_NONE);
  EXPECT_TRUE(out.positionReport.isAltitudeValid);
  EXPECT_NEAR(out.positionReport.altitude, 1000.0f * 0.3048f, 0.01f);
  EXPECT_TRUE(out.positionReport.isGroundSpeedValid);
  EXPECT_NEAR(out.positionReport.groundSpeed, 60.0f * 1852.0f / 3600, 0.01f);
}

TEST(SkyEchoGdl90, FlagsAltitudeAndSpeedThatAreNotAvailable) {
  uint8_t block[28];
  ownship_block(block);
  block[11] = 0xFF;
  block[12] = 0xF9;
  block[14] = 0xFF;
  block[15] = 0xF0;
  gdl90Data_t out{};
  ASSERT_EQ(gdl90DecodeBlock(block, sizeof(block), &out), GDL90_ERR_NONE);
  EXPECT_FALSE(out.positionReport.isAltitudeValid);
  EXPECT_FALSE(out.positionReport.isGroundSpeedValid);
}

}  // namespace esphome::skyecho::testing
