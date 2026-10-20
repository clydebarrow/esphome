#include "threat.h"

#include <algorithm>

#include "riemann.h"

namespace esphome::skyecho {

// How far ahead to look for the closest point of approach and close proximity.
static constexpr float LOOKAHEAD_S = 120.0f;
// 1000 ft of height separation counts the same as 1 NM horizontally.
static constexpr float VERTICAL_WEIGHT = 1852.0f / 304.8f;
// A target moving directly away is scored as this many times further away.
static constexpr float OPENING_FACTOR = 2.0f;

static void velocity(const gdl90PositionReport_t &report, float &north, float &east) {
  if (!report.isTrackValid) {
    north = east = 0.0f;
    return;
  }
  const float track = toRadians(report.track);
  north = report.groundSpeed * cosf(track);
  east = report.groundSpeed * sinf(track);
}

TargetGeometry target_geometry(const gdl90PositionReport_t &ownship, const gdl90PositionReport_t &target) {
  TargetGeometry geometry{};
  geometry.north = trafficNorthing(&ownship, &target);
  geometry.east = trafficEasting(&ownship, &target);
  geometry.altitude = target.altitude - ownship.altitude;
  float own_north, own_east, target_north, target_east;
  velocity(ownship, own_north, own_east);
  velocity(target, target_north, target_east);
  geometry.velocity_north = target_north - own_north;
  geometry.velocity_east = target_east - own_east;
  const float own_vs = ownship.isVerticalSpeedValid ? ownship.verticalSpeed : 0.0f;
  const float target_vs = target.isVerticalSpeedValid ? target.verticalSpeed : 0.0f;
  geometry.vertical_speed = target_vs - own_vs;
  return geometry;
}

// The time of the closest approach within the look-ahead, measured in a space where
// height is stretched by the vertical weight. Sets range_rate, which is positive when
// the separation is growing.
static float approach_time(const TargetGeometry &g, float &range_rate) {
  const float vz = g.vertical_speed * VERTICAL_WEIGHT;
  range_rate = g.east * g.velocity_east + g.north * g.velocity_north + g.altitude * VERTICAL_WEIGHT * vz;
  const float speed_squared = g.velocity_east * g.velocity_east + g.velocity_north * g.velocity_north + vz * vz;
  if (range_rate >= 0.0f || speed_squared <= 0.0f)
    return 0.0f;
  return std::min(-range_rate / speed_squared, LOOKAHEAD_S);
}

float threat_score(const TargetGeometry &geometry) {
  float range_rate;
  const float time = approach_time(geometry, range_rate);
  const float x = geometry.east + geometry.velocity_east * time;
  const float y = geometry.north + geometry.velocity_north * time;
  const float z = (geometry.altitude + geometry.vertical_speed * time) * VERTICAL_WEIGHT;
  const float separation = sqrtf(x * x + y * y + z * z);
  if (range_rate <= 0.0f)
    return separation;
  // Scale the penalty by how directly the target is moving away, so the score does
  // not jump when a passing target stops closing.
  const float pz = geometry.altitude * VERTICAL_WEIGHT;
  const float vz = geometry.vertical_speed * VERTICAL_WEIGHT;
  const float distance = sqrtf(geometry.east * geometry.east + geometry.north * geometry.north + pz * pz);
  const float speed = sqrtf(geometry.velocity_east * geometry.velocity_east +
                            geometry.velocity_north * geometry.velocity_north + vz * vz);
  return separation * (1.0f + (OPENING_FACTOR - 1.0f) * range_rate / (distance * speed));
}

// The time the target is first inside close proximity, or negative if not within the look-ahead.
static float proximity_time(const TargetGeometry &g, const ThreatSettings &settings) {
  // Horizontally: |p + v t| < R between the roots of a t^2 + b t + c = 0.
  const float a = g.velocity_east * g.velocity_east + g.velocity_north * g.velocity_north;
  const float b = 2.0f * (g.east * g.velocity_east + g.north * g.velocity_north);
  const float c = g.east * g.east + g.north * g.north - settings.alarm_distance * settings.alarm_distance;
  float h_start, h_end;
  if (a <= 0.0f) {
    if (c > 0.0f)
      return -1.0f;
    h_start = 0.0f;
    h_end = INFINITY;
  } else {
    const float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f)
      return -1.0f;
    const float root = sqrtf(discriminant);
    h_start = (-b - root) / (2.0f * a);
    h_end = (-b + root) / (2.0f * a);
  }

  // Vertically: |z + vz t| < H.
  float v_start, v_end;
  if (g.vertical_speed == 0.0f) {
    if (fabsf(g.altitude) >= settings.alarm_altitude)
      return -1.0f;
    v_start = 0.0f;
    v_end = INFINITY;
  } else {
    const float t1 = (-settings.alarm_altitude - g.altitude) / g.vertical_speed;
    const float t2 = (settings.alarm_altitude - g.altitude) / g.vertical_speed;
    v_start = std::min(t1, t2);
    v_end = std::max(t1, t2);
  }

  const float start = std::max({h_start, v_start, 0.0f});
  const float end = std::min(h_end, v_end);
  if (start > end || start > LOOKAHEAD_S)
    return -1.0f;
  return start;
}

ThreatAssessment assess_threat(const TargetGeometry &geometry, const ThreatSettings &settings) {
  ThreatAssessment assessment{};
  assessment.score = threat_score(geometry);
  float range_rate;
  const float time = approach_time(geometry, range_rate);
  assessment.approach_time = time;
  const float x = geometry.east + geometry.velocity_east * time;
  const float y = geometry.north + geometry.velocity_north * time;
  assessment.approach_distance = sqrtf(x * x + y * y);
  assessment.approach_altitude = geometry.altitude + geometry.vertical_speed * time;
  assessment.proximity_time = proximity_time(geometry, settings);
  // A target already in close proximity but moving away has passed, so it no longer raises the alarm.
  assessment.alarm = assessment.proximity_time >= 0.0f && assessment.proximity_time <= settings.alarm_time &&
                     !(assessment.proximity_time == 0.0f && range_rate > 0.0f);
  const float distance = sqrtf(geometry.east * geometry.east + geometry.north * geometry.north);
  assessment.ignored = !assessment.alarm && (distance > settings.max_distance ||
                                             fabsf(geometry.altitude) > settings.max_altitude_difference);
  return assessment;
}

ThreatAssessment SignificantTargetSelector::consider(size_t index, const gdl90PositionReport_t &target) {
  const ThreatAssessment assessment = assess_threat(target_geometry(this->ownship_, target), this->settings_);
  if (assessment.ignored)
    return assessment;
  bool better;
  if (this->best_ < 0) {
    better = true;
  } else if (assessment.alarm != this->best_assessment_.alarm) {
    better = assessment.alarm;
  } else if (assessment.alarm) {
    better = assessment.proximity_time < this->best_assessment_.proximity_time;
  } else {
    better = assessment.score < this->best_assessment_.score;
  }
  if (better) {
    this->best_ = static_cast<int>(index);
    this->best_assessment_ = assessment;
  }
  return assessment;
}

}  // namespace esphome::skyecho
