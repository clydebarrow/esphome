#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "esphome/components/skyecho/flarm_nmea.h"

namespace esphome::skyecho::testing {

static constexpr float METERS_PER_DEGREE = 111195.0f;

static gdl90PositionReport_t ownship() {
  gdl90PositionReport_t own;
  memset(&own, 0, sizeof(own));
  own.latitude = -28.2f;
  own.longitude = 152.03f;
  own.altitude = 1000.0f;
  own.isAltitudeValid = true;
  own.groundSpeed = 25.0f;
  own.isGroundSpeedValid = true;
  own.track = 90.0f;
  own.isTrackValid = true;
  own.category = Glider;
  return own;
}

static gdl90PositionReport_t target_north(float north_m, float above_m) {
  gdl90PositionReport_t t = ownship();
  t.address = 0x7C1FD7;
  t.addressType = EmitterAdsbIcao;
  t.latitude += north_m / METERS_PER_DEGREE;
  t.altitude += above_m;
  t.track = 270.0f;
  t.groundSpeed = 30.0f;
  t.verticalSpeed = 1.5f;
  t.isVerticalSpeedValid = true;
  return t;
}

// The sentence between '$' and '*', and its checksum
static bool checksum_ok(const std::string &sentence) {
  const size_t star = sentence.find('*');
  if (sentence[0] != '$' || star == std::string::npos)
    return false;
  uint8_t sum = 0;
  for (size_t i = 1; i < star; i++)
    sum ^= static_cast<uint8_t>(sentence[i]);
  char hex[3];
  snprintf(hex, sizeof(hex), "%02X", sum);
  return sentence.compare(star + 1, 2, hex) == 0 && sentence.compare(star + 3, 2, "\r\n") == 0;
}

static std::string line_starting(const std::string &out, const char *prefix) {
  const size_t start = out.find(prefix);
  if (start == std::string::npos)
    return "";
  return out.substr(start, out.find("\r\n", start) + 2 - start);
}

TEST(SkyEchoFlarmNmea, ChecksumAndLineEnd) {
  // A sentence from the FLARM data port specification
  EXPECT_EQ(nmea_sentence("PFLAU,3,1,2,1,0,,0,,,"), "$PFLAU,3,1,2,1,0,,0,,,*4F\r\n");
  EXPECT_TRUE(checksum_ok(nmea_sentence("PGRMZ,1234,F,2")));
}

TEST(SkyEchoFlarmNmea, TargetPlacedRelativeToTheOwnship) {
  const gdl90PositionReport_t own = ownship();
  const gdl90PositionReport_t target = target_north(1000, 100);
  std::string out;
  append_flarm_traffic(out, own, &target, 1, ThreatSettings{}, NmeaStatus{true, true, false});
  const std::string pflaa = line_starting(out, "$PFLAA");
  ASSERT_TRUE(checksum_ok(pflaa)) << pflaa;
  EXPECT_EQ(pflaa.substr(0, pflaa.find('*')), "$PFLAA,0,1000,0,100,1,7C1FD7,270,,30,1.5,1");
  // Not transmitting, a fix in flight, no alarm
  const std::string pflau = line_starting(out, "$PFLAU");
  EXPECT_EQ(pflau.substr(0, pflau.find('*')), "$PFLAU,1,0,2,1,0,,0,,,");
  EXPECT_LT(out.find("$PFLAU"), out.find("$PFLAA"));
}

TEST(SkyEchoFlarmNmea, AlarmForAHeadOnTarget) {
  gdl90PositionReport_t own = ownship();
  own.track = 0;
  own.groundSpeed = 30;
  // 500 m ahead at the same height, coming straight at us: 250 m apart after a few seconds
  gdl90PositionReport_t target = target_north(500, 0);
  target.track = 180;
  target.isTrackValid = true;
  target.verticalSpeed = 0;
  target.category = Light;
  std::string out;
  append_flarm_traffic(out, own, &target, 1, ThreatSettings{}, NmeaStatus{true, true, false});
  const std::string pflau = line_starting(out, "$PFLAU");
  EXPECT_EQ(pflau.substr(0, pflau.find('*')), "$PFLAU,1,0,2,1,3,0,2,0,500,7C1FD7");
  EXPECT_EQ(line_starting(out, "$PFLAA").substr(0, 8), "$PFLAA,3");
  EXPECT_NE(line_starting(out, "$PFLAA").find(",30,0.0,8*"), std::string::npos) << out;
}

TEST(SkyEchoFlarmNmea, AlarmLevels) {
  ThreatAssessment a{};
  a.alarm = true;
  a.proximity_time = 25;
  EXPECT_EQ(flarm_alarm_level(a), 0);
  a.proximity_time = 20;
  EXPECT_EQ(flarm_alarm_level(a), 1);
  a.proximity_time = 10;
  EXPECT_EQ(flarm_alarm_level(a), 2);
  a.proximity_time = 0;
  EXPECT_EQ(flarm_alarm_level(a), 3);
  a.alarm = false;
  EXPECT_EQ(flarm_alarm_level(a), 0);
}

TEST(SkyEchoFlarmNmea, AircraftTypes) {
  EXPECT_EQ(flarm_aircraft_type(Glider), 1);
  EXPECT_EQ(flarm_aircraft_type(Light), 8);
  EXPECT_EQ(flarm_aircraft_type(Rotorcraft), 3);
  EXPECT_EQ(flarm_aircraft_type(Heavy), 9);
  EXPECT_EQ(flarm_aircraft_type(None), 0);
}

TEST(SkyEchoFlarmNmea, TargetsAreLimited) {
  const gdl90PositionReport_t own = ownship();
  gdl90PositionReport_t targets[3] = {target_north(1000, 0), target_north(2000, 0), target_north(3000, 0)};
  std::string out;
  append_flarm_traffic(out, own, targets, 3, ThreatSettings{}, NmeaStatus{false, false, false}, 2);
  size_t lines = 0;
  for (size_t at = out.find("$PFLAA"); at != std::string::npos; at = out.find("$PFLAA", at + 1))
    lines++;
  EXPECT_EQ(lines, 2u);
  // All three were heard; without a fix GPS is 0
  EXPECT_EQ(line_starting(out, "$PFLAU").substr(0, 15), "$PFLAU,3,0,0,1,");
}

TEST(SkyEchoFlarmNmea, GpsSentences) {
  NmeaFix fix{};
  fix.valid = true;
  fix.latitude = -28.5;
  fix.longitude = 152.25;
  fix.altitude = 450.5f;
  fix.geoid_separation = 38.0f;
  fix.speed = 10.0f;
  fix.course = 45.0f;
  fix.satellites = 9;
  fix.hdop = 0.9f;
  fix.time_valid = true;
  fix.hour = 3;
  fix.minute = 4;
  fix.second = 5;
  fix.date_valid = true;
  fix.year = 2026;
  fix.month = 11;
  fix.day = 20;
  std::string out;
  append_gps_sentences(out, fix);
  const std::string rmc = line_starting(out, "$GPRMC");
  ASSERT_TRUE(checksum_ok(rmc)) << rmc;
  EXPECT_EQ(rmc.substr(0, rmc.find('*')), "$GPRMC,030405.00,A,2830.0000,S,15215.0000,E,19.4,45.0,201126,,,A");
  const std::string gga = line_starting(out, "$GPGGA");
  ASSERT_TRUE(checksum_ok(gga)) << gga;
  EXPECT_EQ(gga.substr(0, gga.find('*')), "$GPGGA,030405.00,2830.0000,S,15215.0000,E,1,09,0.9,450.5,M,38.0,M,,");

  fix.valid = false;
  out.clear();
  append_gps_sentences(out, fix);
  EXPECT_TRUE(out.empty());
}

TEST(SkyEchoFlarmNmea, PressureAltitude) {
  std::string out;
  append_pgrmz(out, 304.8f);
  EXPECT_EQ(out.substr(0, out.find('*')), "$PGRMZ,1000,F,2");
}

}  // namespace esphome::skyecho::testing
