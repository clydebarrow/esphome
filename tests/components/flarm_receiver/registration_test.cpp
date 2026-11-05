#include <gtest/gtest.h>

#include "esphome/components/flarm_receiver/flarm_receiver.h"

namespace esphome::flarm_receiver::testing {

TEST(FlarmRegistration, AustralianAddress) {
  uint32_t address = 0;
  ASSERT_TRUE(FlarmReceiver::australian_address("VH-GKP", address));
  EXPECT_EQ(address, 0x7C1FD7u);
  ASSERT_TRUE(FlarmReceiver::australian_address("gkp", address));
  EXPECT_EQ(address, 0x7C1FD7u);
  ASSERT_TRUE(FlarmReceiver::australian_address("VHOQA", address));
  EXPECT_EQ(address, 0x7C4920u);
  ASSERT_TRUE(FlarmReceiver::australian_address("AAA", address));
  EXPECT_EQ(address, 0x7C0000u);
  ASSERT_TRUE(FlarmReceiver::australian_address("999", address));
  EXPECT_EQ(address, 0x7CB63Fu);
}

TEST(FlarmRegistration, RejectsOthers) {
  uint32_t address = 0;
  EXPECT_FALSE(FlarmReceiver::australian_address("", address));
  EXPECT_FALSE(FlarmReceiver::australian_address("GK", address));
  EXPECT_FALSE(FlarmReceiver::australian_address("VH-GKPX", address));
  EXPECT_FALSE(FlarmReceiver::australian_address("G-KP", address));
  EXPECT_FALSE(FlarmReceiver::australian_address("D-1234", address));
}

}  // namespace esphome::flarm_receiver::testing
