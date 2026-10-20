#include <gtest/gtest.h>

#include "esphome/components/skyecho/threat.h"

namespace esphome::skyecho::testing {

static constexpr float NM = 1852.0f;
static constexpr float FT = 0.3048f;

static TargetGeometry at(float north, float east, float altitude) {
  TargetGeometry geometry{};
  geometry.north = north;
  geometry.east = east;
  geometry.altitude = altitude;
  return geometry;
}

TEST(SkyEchoThreat, StationaryTargetScoresItsDistance) { EXPECT_FLOAT_EQ(threat_score(at(NM, 0.0f, 0.0f)), NM); }

TEST(SkyEchoThreat, ThousandFeetCountsAsOneNauticalMile) {
  EXPECT_NEAR(threat_score(at(0.0f, 0.0f, 1000.0f * FT)), NM, 1.0f);
}

TEST(SkyEchoThreat, NearTargetAboveMattersMoreThanADistantLevelOne) {
  // 1 NM away and 2000 ft above needs more attention than 8 NM away at the same height.
  EXPECT_LT(threat_score(at(NM, 0.0f, 2000.0f * FT)), threat_score(at(8.0f * NM, 0.0f, 0.0f)));
}

TEST(SkyEchoThreat, ClosingTargetScoresSeparationAtClosestApproach) {
  // 3 NM ahead, passing 1 NM to the east, closing at 100 m/s: closest in 3 NM / 100 m/s.
  TargetGeometry geometry = at(3.0f * NM, NM, 0.0f);
  geometry.velocity_north = -100.0f;
  EXPECT_NEAR(threat_score(geometry), NM, 1.0f);
}

TEST(SkyEchoThreat, ClosestApproachIsLimitedToTheLookAhead) {
  // Closing at 10 m/s from 3 NM: within the two minute look-ahead it closes by only 1200 m.
  TargetGeometry geometry = at(3.0f * NM, 0.0f, 0.0f);
  geometry.velocity_north = -10.0f;
  EXPECT_NEAR(threat_score(geometry), 3.0f * NM - 1200.0f, 1.0f);
}

TEST(SkyEchoThreat, TargetMovingDirectlyAwayScoresTwiceItsDistance) {
  TargetGeometry geometry = at(NM, 0.0f, 0.0f);
  geometry.velocity_north = 50.0f;
  EXPECT_FLOAT_EQ(threat_score(geometry), 2.0f * NM);
}

TEST(SkyEchoThreat, OpeningPenaltyGrowsWithHowDirectlyTheTargetMovesAway) {
  // Moving away at 45 degrees: half way between no penalty and the full one.
  TargetGeometry geometry = at(NM, 0.0f, 0.0f);
  geometry.velocity_north = 50.0f;
  geometry.velocity_east = 50.0f;
  EXPECT_NEAR(threat_score(geometry), NM * (1.0f + 0.70710678f), 1.0f);
}

TEST(SkyEchoThreat, ClosingTargetOutranksANearerOpeningOne) {
  TargetGeometry opening = at(NM, 0.0f, 0.0f);
  opening.velocity_north = 50.0f;
  TargetGeometry closing = at(1.5f * NM, 0.0f, 0.0f);
  closing.velocity_north = -50.0f;
  EXPECT_LT(threat_score(closing), threat_score(opening));
}

TEST(SkyEchoThreat, ClimbingTowardsOwnshipCountsAsClosing) {
  TargetGeometry geometry = at(0.0f, 0.0f, -1000.0f * FT);
  geometry.vertical_speed = 5.0f;
  EXPECT_LT(threat_score(geometry), threat_score(at(0.0f, 0.0f, -1000.0f * FT)));
}

static gdl90PositionReport_t report(float latitude, float altitude, float speed, float track) {
  gdl90PositionReport_t result{};
  result.latitude = latitude;
  result.longitude = 151.0f;
  result.altitude = altitude;
  result.groundSpeed = speed;
  result.track = track;
  result.isTrackValid = true;
  return result;
}

TEST(SkyEchoThreat, GeometryIsRelativeToTheOwnship) {
  const gdl90PositionReport_t ownship = report(-28.0f, 1000.0f, 50.0f, 0.0f);
  const gdl90PositionReport_t target = report(-27.99f, 1300.0f, 50.0f, 180.0f);
  const TargetGeometry geometry = target_geometry(ownship, target);
  EXPECT_GT(geometry.north, 1000.0f);
  EXPECT_NEAR(geometry.east, 0.0f, 1.0f);
  EXPECT_FLOAT_EQ(geometry.altitude, 300.0f);
  EXPECT_NEAR(geometry.velocity_north, -100.0f, 0.01f);
}

TEST(SkyEchoThreat, InvalidTrackAndVerticalSpeedCountAsNoMovement) {
  const gdl90PositionReport_t ownship = report(-28.0f, 1000.0f, 0.0f, 0.0f);
  gdl90PositionReport_t target = report(-27.99f, 1000.0f, 50.0f, 180.0f);
  target.isTrackValid = false;
  target.verticalSpeed = -1331.0f;  // what the decoder leaves for "no data"
  target.isVerticalSpeedValid = false;
  const TargetGeometry geometry = target_geometry(ownship, target);
  EXPECT_FLOAT_EQ(geometry.velocity_north, 0.0f);
  EXPECT_FLOAT_EQ(geometry.velocity_east, 0.0f);
  EXPECT_FLOAT_EQ(geometry.vertical_speed, 0.0f);
}

static TargetGeometry closing_north(float distance, float altitude, float speed) {
  TargetGeometry geometry = at(distance, 0.0f, altitude);
  geometry.velocity_north = -speed;
  return geometry;
}

TEST(SkyEchoThreat, ClosestApproachIsReported) {
  // 3 NM ahead, passing 1 NM to the east, 300 ft above, closing at 100 m/s.
  TargetGeometry geometry = at(3.0f * NM, NM, 300.0f * FT);
  geometry.velocity_north = -100.0f;
  const ThreatAssessment assessment = assess_threat(geometry, ThreatSettings{});
  EXPECT_NEAR(assessment.approach_time, 3.0f * NM / 100.0f, 0.1f);
  EXPECT_NEAR(assessment.approach_distance, NM, 1.0f);
  EXPECT_NEAR(assessment.approach_altitude, 300.0f * FT, 0.1f);
}

TEST(SkyEchoThreat, HeadOnTargetRaisesTheAlarmThirtySecondsOut) {
  const ThreatSettings settings{};
  // Close proximity starts 0.5 NM out; at 150 m/s that is 30 s away from 0.5 NM + 4500 m.
  const ThreatAssessment early = assess_threat(closing_north(926.0f + 4600.0f, 0.0f, 150.0f), settings);
  EXPECT_NEAR(early.proximity_time, 4600.0f / 150.0f, 0.1f);
  EXPECT_FALSE(early.alarm);
  const ThreatAssessment late = assess_threat(closing_north(926.0f + 4400.0f, 0.0f, 150.0f), settings);
  EXPECT_NEAR(late.proximity_time, 4400.0f / 150.0f, 0.1f);
  EXPECT_TRUE(late.alarm);
}

TEST(SkyEchoThreat, TargetPassingClearRaisesNoAlarm) {
  TargetGeometry wide = at(2.0f * NM, NM, 0.0f);
  wide.velocity_north = -100.0f;
  EXPECT_LT(assess_threat(wide, ThreatSettings{}).proximity_time, 0.0f);
  // Straight at the ownship, but 1000 ft above.
  EXPECT_LT(assess_threat(closing_north(NM, 1000.0f * FT, 100.0f), ThreatSettings{}).proximity_time, 0.0f);
}

TEST(SkyEchoThreat, DescendingTargetReachesProximityWhenWithinTheHeightLimit) {
  // Overhead, 1000 ft above and coming down at 10 m/s: within 500 ft after 15.24 s.
  TargetGeometry geometry = at(0.0f, 0.0f, 1000.0f * FT);
  geometry.vertical_speed = -10.0f;
  const ThreatAssessment assessment = assess_threat(geometry, ThreatSettings{});
  EXPECT_NEAR(assessment.proximity_time, 500.0f * FT / 10.0f, 0.01f);
  EXPECT_TRUE(assessment.alarm);
}

TEST(SkyEchoThreat, TargetAlreadyCloseRaisesTheAlarmNow) {
  const ThreatAssessment assessment = assess_threat(at(300.0f, 0.0f, 100.0f * FT), ThreatSettings{});
  EXPECT_FLOAT_EQ(assessment.proximity_time, 0.0f);
  EXPECT_TRUE(assessment.alarm);
}

TEST(SkyEchoThreat, TargetThatHasPassedRaisesNoAlarm) {
  TargetGeometry geometry = at(-300.0f, 0.0f, 100.0f * FT);
  geometry.velocity_north = -100.0f;
  const ThreatAssessment assessment = assess_threat(geometry, ThreatSettings{});
  EXPECT_FLOAT_EQ(assessment.proximity_time, 0.0f);
  EXPECT_FALSE(assessment.alarm);
}

TEST(SkyEchoThreat, AlarmThresholdsAreConfigurable) {
  ThreatSettings settings{};
  settings.alarm_distance = NM;
  settings.alarm_time = 60.0f;
  const ThreatAssessment assessment = assess_threat(closing_north(NM + 5000.0f, 0.0f, 100.0f), settings);
  EXPECT_NEAR(assessment.proximity_time, 50.0f, 0.1f);
  EXPECT_TRUE(assessment.alarm);
}

TEST(SkyEchoThreat, FiltersIgnoreDistantTargets) {
  ThreatSettings settings{};
  settings.max_distance = 10.0f * NM;
  settings.max_altitude_difference = 3000.0f * FT;
  EXPECT_FALSE(assess_threat(at(9.0f * NM, 0.0f, 2000.0f * FT), settings).ignored);
  EXPECT_TRUE(assess_threat(at(11.0f * NM, 0.0f, 0.0f), settings).ignored);
  EXPECT_TRUE(assess_threat(at(NM, 0.0f, -4000.0f * FT), settings).ignored);
}

TEST(SkyEchoThreat, AlarmIsNeverFilteredOut) {
  ThreatSettings settings{};
  settings.max_distance = 100.0f;
  const ThreatAssessment assessment = assess_threat(at(300.0f, 0.0f, 0.0f), settings);
  EXPECT_TRUE(assessment.alarm);
  EXPECT_FALSE(assessment.ignored);
}

TEST(SkyEchoThreat, SelectorPicksTheMostSignificantTarget) {
  const gdl90PositionReport_t ownship = report(-28.0f, 1000.0f, 50.0f, 0.0f);
  const ThreatSettings settings{};
  SignificantTargetSelector selector(ownship, settings);
  EXPECT_EQ(selector.get_best(), -1);
  // Nearest, but 3000 ft above and flying away.
  selector.consider(0, report(-27.995f, 1000.0f + 3000.0f * FT, 80.0f, 0.0f));
  // Further, at the same height and heading straight for the ownship.
  selector.consider(1, report(-27.97f, 1000.0f, 50.0f, 180.0f));
  // Further still, at the same height but flying away.
  selector.consider(2, report(-27.96f, 1000.0f, 80.0f, 0.0f));
  EXPECT_EQ(selector.get_best(), 1);
}

TEST(SkyEchoThreat, SelectorPutsAnAlarmFirstAndSkipsIgnoredTargets) {
  const gdl90PositionReport_t ownship = report(-28.0f, 1000.0f, 0.0f, 0.0f);
  ThreatSettings settings{};
  settings.max_altitude_difference = 3000.0f * FT;
  SignificantTargetSelector selector(ownship, settings);
  // Overhead and well clear vertically, but beyond the altitude filter.
  selector.consider(0, report(-28.0f, 1000.0f + 4000.0f * FT, 0.0f, 0.0f));
  EXPECT_EQ(selector.get_best(), -1);
  // Level and close, but not moving: the lowest score.
  selector.consider(1, report(-27.985f, 1000.0f, 0.0f, 0.0f));
  // Further away, but heading straight for the ownship: predicted close in under 30 s.
  selector.consider(2, report(-27.95f, 1000.0f, 200.0f, 180.0f));
  EXPECT_EQ(selector.get_best(), 2);
  EXPECT_TRUE(selector.get_best_assessment().alarm);
}

}  // namespace esphome::skyecho::testing
