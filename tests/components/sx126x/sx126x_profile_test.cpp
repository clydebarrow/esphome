#include "sx126x_test_helpers.h"

namespace esphome::sx126x::testing {

// The last SetPacketType command sent, or -1
static int last_packet_type(const RadioHardware &hw) {
  int type = -1;
  for (const auto &command : hw.transactions) {
    if (command.size() >= 2 && command[0] == RADIO_SET_PACKETTYPE)
      type = command[1];
  }
  return type;
}

static RadioProfile fsk_profile(const SX126x &radio) {
  RadioProfile profile = radio.get_requested_profile();
  profile.modulation = PACKET_TYPE_GFSK;
  profile.bandwidth = SX126X_BW_234300;
  profile.bitrate = 100000;
  profile.deviation = 50000;
  profile.payload_length = 3;
  profile.rx_start = true;
  return profile;
}

// Another profile replaces the configured one until it is released, and only its owner hears packets meanwhile
TEST(SX126xAlternateProfile, OwnerHearsPacketsUntilReleased) {
  RadioHardware hw;
  PacketListener link, owner;
  hw.configure(true, true);
  hw.radio.register_listener(&link);
  hw.radio.register_listener(&owner);
  EXPECT_EQ(last_packet_type(hw), PACKET_TYPE_LORA);

  const RadioProfile fsk = fsk_profile(hw.radio);
  hw.clear_trace();
  ASSERT_TRUE(hw.radio.apply_profile(&fsk, &owner));
  EXPECT_EQ(last_packet_type(hw), PACKET_TYPE_GFSK);
  EXPECT_EQ(hw.radio.get_profile_owner(), &owner);
  EXPECT_TRUE(hw.receiving);
  hw.irq = IRQ_RX_DONE;
  hw.radio.loop();
  EXPECT_EQ(owner.packets.size(), 1u);
  EXPECT_TRUE(link.packets.empty());

  hw.clear_trace();
  ASSERT_TRUE(hw.radio.apply_profile(nullptr, nullptr));
  EXPECT_EQ(last_packet_type(hw), PACKET_TYPE_LORA);
  EXPECT_EQ(hw.radio.get_profile_owner(), nullptr);
  hw.irq = IRQ_RX_DONE;
  hw.radio.loop();
  EXPECT_EQ(owner.packets.size(), 2u);
  EXPECT_EQ(link.packets.size(), 1u);
  // The configured profile is untouched
  EXPECT_EQ(hw.radio.get_requested_profile().modulation, PACKET_TYPE_LORA);
}

// A configuration retried after a failure keeps the profile that was applied
TEST(SX126xAlternateProfile, ReconfigurationKeepsTheAppliedProfile) {
  RadioHardware hw;
  PacketListener owner;
  hw.configure(true, true);
  const RadioProfile fsk = fsk_profile(hw.radio);
  ASSERT_TRUE(hw.radio.apply_profile(&fsk, &owner));
  hw.clear_trace();
  hw.radio.configure();
  EXPECT_EQ(last_packet_type(hw), PACKET_TYPE_GFSK);
}

// Retuning sets the frequency and receives again, without a reset or a new configuration
TEST(SX126xAlternateProfile, RetuneChangesOnlyTheFrequency) {
  RadioHardware hw;
  hw.configure(false, true);
  const unsigned resets = hw.rst.writes;
  hw.clear_trace();
  hw.radio.retune(917400000);
  EXPECT_EQ(hw.rst.writes, resets);
  ASSERT_GE(hw.transactions.size(), 3u);
  EXPECT_EQ(hw.transactions[0][0], RADIO_SET_STANDBY);
  bool frequency = false;
  for (const auto &command : hw.transactions) {
    EXPECT_NE(command[0], RADIO_SET_PACKETTYPE);
    if (command[0] == RADIO_SET_RFFREQUENCY) {
      frequency = true;
      const uint32_t steps =
          (uint32_t(command[1]) << 24) | (uint32_t(command[2]) << 16) | (uint32_t(command[3]) << 8) | command[4];
      EXPECT_EQ(steps, uint32_t((uint64_t(917400000) << 25) / XTAL_FREQ));
    }
  }
  EXPECT_TRUE(frequency);
  EXPECT_TRUE(hw.receiving);
}

// A send in progress keeps its profile
TEST(SX126xAlternateProfile, NotWhileSending) {
  RadioHardware hw;
  PacketListener owner;
  hw.configure(true, true);
  hw.auto_complete = false;
  struct Completion final : SX126xTransmitListener {
    void on_transmit_complete(SX126xError result) override {}
  } listener;
  ASSERT_EQ(hw.radio.start_transmit_async(&listener, hw.packet), SX126xError::SX126X_ERROR_NONE);
  const RadioProfile fsk = fsk_profile(hw.radio);
  EXPECT_FALSE(hw.radio.apply_profile(&fsk, &owner));
  EXPECT_EQ(hw.radio.get_profile_owner(), nullptr);
  hw.radio.cancel_transmit_async(&listener);
}

}  // namespace esphome::sx126x::testing
