#include <gtest/gtest.h>

#include <cstring>

#include "esphome/components/skyecho/flarm_target.h"

namespace esphome::skyecho::testing {

static gdl90PositionReport_t ownship() {
  gdl90PositionReport_t own;
  memset(&own, 0, sizeof(own));
  own.latitude = -31.42f;
  own.longitude = 152.75f;
  own.altitude = 600.0f;  // pressure altitude
  own.isAltitudeValid = true;
  own.groundSpeed = 40.0f;
  own.track = 90.0f;
  own.isTrackValid = true;
  return own;
}

static FlarmTarget target(uint32_t address, float north_m, float altitude_hae) {
  FlarmTarget t{};
  t.address = address;
  t.icao = true;
  t.aircraft_type = 1;
  t.latitude = -31.42f + north_m / 111195.0f;
  t.longitude = 152.75f;
  t.altitude_hae = altitude_hae;
  t.speed = 40.0f;
  t.track = 90.0f;
  t.climb_rate = 1.5f;
  return t;
}

TEST(SkyEchoFlarmTarget, AustralianRegistrationAsCallsign) {
  char callsign[GDL_CALLSIGN_LENGTH + 1];
  flarm_callsign(target(0x7C1FD7, 0, 0), callsign);
  EXPECT_STREQ(callsign, "GKP");
  FlarmTarget other = target(0x3D1234, 0, 0);
  flarm_callsign(other, callsign);
  EXPECT_STREQ(callsign, "3D1234");
  // A FLARM ID in the Australian range is not a registration
  other = target(0x7C1FD7, 0, 0);
  other.icao = false;
  flarm_callsign(other, callsign);
  EXPECT_STREQ(callsign, "7C1FD7");
}

TEST(SkyEchoFlarmTarget, AltitudeOnTheOwnshipsPressureScale) {
  // The ownship is 30 m higher by GPS than by pressure, so the target is put 30 m lower
  const gdl90PositionReport_t report = flarm_report(target(0x7C1FD7, 1000, 730), ownship(), true, 630.0f);
  EXPECT_NEAR(report.altitude, 700.0f, 0.01f);
  EXPECT_EQ(report.addressType, EmitterAdsbIcao);
  EXPECT_EQ(report.category, Glider);
  EXPECT_TRUE(report.isAltitudeValid);
  EXPECT_TRUE(report.isTrackValid);
  EXPECT_TRUE(report.isInFlight);
  EXPECT_FLOAT_EQ(report.verticalSpeed, 1.5f);
  // Without the ownship's GPS altitude, the FLARM height as it is
  EXPECT_NEAR(flarm_report(target(1, 1000, 730), ownship(), false, 0).altitude, 730.0f, 0.01f);
}

TEST(SkyEchoFlarmTarget, Categories) {
  EXPECT_EQ(flarm_category(2), Light);
  EXPECT_EQ(flarm_category(3), Rotorcraft);
  EXPECT_EQ(flarm_category(7), HangGlider);
  EXPECT_EQ(flarm_category(0), None);
}

TEST(SkyEchoFlarmTarget, OwnFlarmIsLearnedAfterStayingClose) {
  OwnFlarmFilter filter;
  const gdl90PositionReport_t own = ownship();
  const gdl90PositionReport_t here = flarm_report(target(0x7C2000, 10, 610), own, false, 0);
  uint32_t address;
  uint32_t now = 1000;
  for (; now < 1000 + OwnFlarmFilter::LEARN_MS; now += 1000) {
    EXPECT_TRUE(filter.is_own(own, here, now));
    EXPECT_FALSE(filter.get_own_address(&address));
  }
  EXPECT_TRUE(filter.is_own(own, here, now));
  ASSERT_TRUE(filter.get_own_address(&address));
  EXPECT_EQ(address, 0x7C2000u);
  // Once learned, it is dropped wherever it seems to be
  EXPECT_TRUE(filter.is_own(own, flarm_report(target(0x7C2000, 500, 610), own, false, 0), now));
  // Others away from the ownship are kept
  EXPECT_FALSE(filter.is_own(own, flarm_report(target(0x7C2001, 500, 610), own, false, 0), now));
}

TEST(SkyEchoFlarmTarget, PassingCloseIsNotLearned) {
  OwnFlarmFilter filter;
  const gdl90PositionReport_t own = ownship();
  uint32_t address;
  // Close for a few seconds, then away: dropped while close, but not taken as the ownship's
  for (uint32_t now = 0; now < 4000; now += 1000)
    EXPECT_TRUE(filter.is_own(own, flarm_report(target(5, 5, 600), own, false, 0), now));
  EXPECT_FALSE(filter.is_own(own, flarm_report(target(5, 200, 600), own, false, 0), 4000));
  for (uint32_t now = 5000; now < 5000 + OwnFlarmFilter::LEARN_MS - 1000; now += 1000)
    EXPECT_TRUE(filter.is_own(own, flarm_report(target(5, 5, 600), own, false, 0), now));
  EXPECT_FALSE(filter.get_own_address(&address));
  // Close by distance but far above is not the ownship
  EXPECT_FALSE(filter.is_own(own, flarm_report(target(6, 5, 700), own, false, 0), 20000));
}

}  // namespace esphome::skyecho::testing
