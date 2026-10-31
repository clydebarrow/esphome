#include "threat.h"

#include <algorithm>

#include "riemann.h"

namespace esphome::skyecho {

// How far ahead to look for the closest point of approach and close proximity.
static constexpr float LOOKAHEAD_S = 120.0f;
// Turning paths are followed in steps of this long, with straight lines between them.
static constexpr float PATH_STEP_S = 2.0f;
static constexpr size_t PATH_STEPS = static_cast<size_t>(LOOKAHEAD_S / PATH_STEP_S);
// 1000 ft of height separation counts the same as 1 NM horizontally.
static constexpr float VERTICAL_WEIGHT = 1852.0f / 304.8f;
// A target moving directly away is scored as this many times further away.
static constexpr float OPENING_FACTOR = 2.0f;
// Below this relative ground speed squared, (0.5 m/s)^2, a target counts as not moving horizontally.
static constexpr float MIN_SPEED_SQUARED = 0.25f;

// Turn rate estimation, in degrees and seconds.
static constexpr float TURN_SMOOTHING_S = 2.0f;  // time constant of the smoothing
static constexpr float MIN_TURN_GAP_S = 0.2f;    // reports closer than this keep the previous estimate
static constexpr float MAX_TURN_GAP_S = 5.0f;    // reports further apart than this give no estimate
static constexpr float MIN_TURN_SPEED = 5.0f;    // m/s; slower than this, the track is too noisy
static constexpr float MAX_TURN_RATE = 30.0f;
static constexpr float MIN_TURN_RATE = 1.5f;  // anything slower is taken as flying straight

// Meters per degree of latitude (approx), used for dead reckoning.
static constexpr float METERS_PER_DEGREE = 111111.0f;

void extrapolate_report(gdl90PositionReport_t &report, float dt) {
  if (dt <= 0.0f || report.groundSpeed <= 0.0f || !report.isTrackValid)
    return;
  float distance = report.groundSpeed * dt;  // meters travelled since the report
  // Follow a turn by moving along the track half way through it, then turning.
  float track = toRadians(report.track + report.turnRate * dt / 2.0f);
  report.track = fmodf(report.track + report.turnRate * dt + 360.0f, 360.0f);
  float delta_north = distance * cosf(track);
  float delta_east = distance * sinf(track);
  report.latitude += delta_north / METERS_PER_DEGREE;
  report.longitude += delta_east / (METERS_PER_DEGREE * cosf(toRadians(report.latitude)));
}

uint16_t ThreatWarnings::check(const Threat &threat) {
  if (!threat.valid || !threat.assessment.alarm) {
    this->address_ = 0;
    return 0;
  }
  if (threat.report.address != this->address_) {
    this->address_ = threat.report.address;
    this->next_ = 0;
  }
  uint16_t due = 0;
  while (this->next_ != this->times_.size() && threat.assessment.proximity_time <= this->times_[this->next_])
    due = this->times_[this->next_++];
  return due;
}

static bool is_glider(emitterCategory_t category) { return category == Glider || category == HangGlider; }

float estimate_turn_rate(const gdl90PositionReport_t &previous, const gdl90PositionReport_t &current, float dt) {
  if (!previous.isTrackValid || !current.isTrackValid || current.groundSpeed < MIN_TURN_SPEED || dt > MAX_TURN_GAP_S)
    return 0.0f;
  if (dt < MIN_TURN_GAP_S)
    return previous.turnRate;
  float change = fmodf(current.track - previous.track, 360.0f);
  if (change > 180.0f) {
    change -= 360.0f;
  } else if (change < -180.0f) {
    change += 360.0f;
  }
  const float rate = std::clamp(change / dt, -MAX_TURN_RATE, MAX_TURN_RATE);
  return previous.turnRate + (rate - previous.turnRate) * dt / (TURN_SMOOTHING_S + dt);
}

static void velocity(const gdl90PositionReport_t &report, float &north, float &east) {
  if (!report.isTrackValid) {
    north = east = 0.0f;
    return;
  }
  const float track = toRadians(report.track);
  north = report.groundSpeed * cosf(track);
  east = report.groundSpeed * sinf(track);
}

static float turn_rate(const gdl90PositionReport_t &report) {
  if (!report.isTrackValid || fabsf(report.turnRate) < MIN_TURN_RATE)
    return 0.0f;
  return toRadians(report.turnRate);
}

TargetGeometry target_geometry(const gdl90PositionReport_t &ownship, const gdl90PositionReport_t &target) {
  TargetGeometry geometry{};
  geometry.north = trafficNorthing(&ownship, &target);
  geometry.east = trafficEasting(&ownship, &target);
  geometry.altitude = target.altitude - ownship.altitude;
  float target_north, target_east;
  velocity(ownship, geometry.own_velocity_north, geometry.own_velocity_east);
  velocity(target, target_north, target_east);
  geometry.velocity_north = target_north - geometry.own_velocity_north;
  geometry.velocity_east = target_east - geometry.own_velocity_east;
  const float own_vs = ownship.isVerticalSpeedValid ? ownship.verticalSpeed : 0.0f;
  const float target_vs = target.isVerticalSpeedValid ? target.verticalSpeed : 0.0f;
  geometry.vertical_speed = target_vs - own_vs;
  geometry.turn_rate = turn_rate(target);
  geometry.own_turn_rate = turn_rate(ownship);
  geometry.gliders = is_glider(ownship.category) && is_glider(target.category);
  return geometry;
}

namespace {

// One aircraft flying at a constant speed and turn rate, moved one step at a time.
class Turn {
 public:
  Turn(float north, float east, float rate, float step) : north_(north), east_(east) {
    const float half = rate * step / 2.0f;
    // The chord of an arc is shorter than the arc by sin(half) / half, and points half way round it.
    const float chord = half == 0.0f ? step : step * sinf(half) / half;
    this->chord_cos_ = chord * cosf(half);
    this->chord_sin_ = chord * sinf(half);
    this->cos_ = cosf(2.0f * half);
    this->sin_ = sinf(2.0f * half);
  }

  // Move one step, adding the distance moved to north and east.
  void step(float &north, float &east) {
    north += this->chord_cos_ * this->north_ - this->chord_sin_ * this->east_;
    east += this->chord_cos_ * this->east_ + this->chord_sin_ * this->north_;
    const float next_north = this->cos_ * this->north_ - this->sin_ * this->east_;
    this->east_ = this->cos_ * this->east_ + this->sin_ * this->north_;
    this->north_ = next_north;
  }

 protected:
  float north_, east_;  // velocity
  float chord_cos_, chord_sin_, cos_, sin_;
};

// The target's horizontal position relative to the ownship over the look-ahead,
// as points joined by straight lines. When neither aircraft turns the relative
// path is straight, so a single line is exact.
struct RelativePath {
  explicit RelativePath(const TargetGeometry &g) {
    this->north[0] = g.north;
    this->east[0] = g.east;
    if (g.turn_rate == 0.0f && g.own_turn_rate == 0.0f) {
      this->steps = 1;
      this->step_time = LOOKAHEAD_S;
      this->north[1] = g.north + g.velocity_north * LOOKAHEAD_S;
      this->east[1] = g.east + g.velocity_east * LOOKAHEAD_S;
      return;
    }
    this->steps = PATH_STEPS;
    this->step_time = PATH_STEP_S;
    Turn own(g.own_velocity_north, g.own_velocity_east, g.own_turn_rate, PATH_STEP_S);
    Turn target(g.velocity_north + g.own_velocity_north, g.velocity_east + g.own_velocity_east, g.turn_rate,
                PATH_STEP_S);
    for (size_t i = 0; i != PATH_STEPS; i++) {
      float own_north = 0.0f, own_east = 0.0f;
      this->north[i + 1] = this->north[i];
      this->east[i + 1] = this->east[i];
      own.step(own_north, own_east);
      target.step(this->north[i + 1], this->east[i + 1]);
      this->north[i + 1] -= own_north;
      this->east[i + 1] -= own_east;
    }
  }

  float north[PATH_STEPS + 1];
  float east[PATH_STEPS + 1];
  size_t steps;
  float step_time;
};

}  // namespace

// The time of the closest approach within the look-ahead: when the target is nearest
// horizontally, or when it passes the ownship's height if it has no horizontal
// movement. Sets distance to the horizontal separation then, and range_rate, which is
// positive when the separation, with height stretched by the vertical weight, is growing.
static float approach_time(const TargetGeometry &g, const RelativePath &path, float &distance, float &range_rate) {
  range_rate = g.east * g.velocity_east + g.north * g.velocity_north +
               g.altitude * g.vertical_speed * VERTICAL_WEIGHT * VERTICAL_WEIGHT;
  if (g.velocity_east * g.velocity_east + g.velocity_north * g.velocity_north < MIN_SPEED_SQUARED) {
    distance = sqrtf(g.east * g.east + g.north * g.north);
    if (g.vertical_speed == 0.0f)
      return 0.0f;
    return std::clamp(-g.altitude / g.vertical_speed, 0.0f, LOOKAHEAD_S);
  }
  float best_time = 0.0f;
  float best = g.east * g.east + g.north * g.north;
  for (size_t i = 0; i != path.steps; i++) {
    const float north = path.north[i];
    const float east = path.east[i];
    const float d_north = path.north[i + 1] - north;
    const float d_east = path.east[i + 1] - east;
    const float length_squared = d_north * d_north + d_east * d_east;
    if (length_squared <= 0.0f)
      continue;
    const float fraction = std::clamp(-(north * d_north + east * d_east) / length_squared, 0.0f, 1.0f);
    const float n = north + d_north * fraction;
    const float e = east + d_east * fraction;
    const float separation = n * n + e * e;
    if (separation < best) {
      best = separation;
      best_time = (static_cast<float>(i) + fraction) * path.step_time;
    }
  }
  distance = sqrtf(best);
  return best_time;
}

static float score(const TargetGeometry &geometry, float time, float distance, float range_rate) {
  const float z = (geometry.altitude + geometry.vertical_speed * time) * VERTICAL_WEIGHT;
  const float separation = sqrtf(distance * distance + z * z);
  if (time > 0.0f || range_rate <= 0.0f)
    return separation;
  // Scale the penalty by how directly the target is moving away, so the score does
  // not jump when a passing target stops closing.
  const float pz = geometry.altitude * VERTICAL_WEIGHT;
  const float vz = geometry.vertical_speed * VERTICAL_WEIGHT;
  const float range = sqrtf(geometry.east * geometry.east + geometry.north * geometry.north + pz * pz);
  const float speed = sqrtf(geometry.velocity_east * geometry.velocity_east +
                            geometry.velocity_north * geometry.velocity_north + vz * vz);
  return separation * (1.0f + (OPENING_FACTOR - 1.0f) * range_rate / (range * speed));
}

float threat_score(const TargetGeometry &geometry) {
  const RelativePath path(geometry);
  float distance, range_rate;
  const float time = approach_time(geometry, path, distance, range_rate);
  return score(geometry, time, distance, range_rate);
}

// The time the target is first inside close proximity, or negative if not within the look-ahead.
static float proximity_time(const TargetGeometry &g, const RelativePath &path, const ThreatSettings &settings) {
  // Vertically: |z + vz t| < H.
  const float alarm_altitude = g.gliders ? settings.glider_alarm_altitude : settings.alarm_altitude;
  float v_start, v_end;
  if (g.vertical_speed == 0.0f) {
    if (fabsf(g.altitude) >= alarm_altitude)
      return -1.0f;
    v_start = 0.0f;
    v_end = INFINITY;
  } else {
    const float t1 = (-alarm_altitude - g.altitude) / g.vertical_speed;
    const float t2 = (alarm_altitude - g.altitude) / g.vertical_speed;
    v_start = std::min(t1, t2);
    v_end = std::max(t1, t2);
  }

  // Horizontally, along each line of the path: |p + d u| < R between the roots of a u^2 + b u + c = 0.
  const float radius_squared = settings.alarm_distance * settings.alarm_distance;
  for (size_t i = 0; i != path.steps; i++) {
    const float north = path.north[i];
    const float east = path.east[i];
    const float d_north = path.north[i + 1] - north;
    const float d_east = path.east[i + 1] - east;
    const float a = d_north * d_north + d_east * d_east;
    const float b = 2.0f * (north * d_north + east * d_east);
    const float c = north * north + east * east - radius_squared;
    float h_start, h_end;
    if (a <= 0.0f) {
      if (c > 0.0f)
        continue;
      h_start = 0.0f;
      h_end = 1.0f;
    } else {
      const float discriminant = b * b - 4.0f * a * c;
      if (discriminant < 0.0f)
        continue;
      const float root = sqrtf(discriminant);
      h_start = std::max((-b - root) / (2.0f * a), 0.0f);
      h_end = std::min((-b + root) / (2.0f * a), 1.0f);
    }
    const float offset = static_cast<float>(i);
    const float start = std::max((offset + h_start) * path.step_time, v_start);
    const float end = std::min((offset + h_end) * path.step_time, v_end);
    if (start <= end && h_start <= h_end)
      return std::max(start, 0.0f);
  }
  return -1.0f;
}

ThreatAssessment assess_threat(const TargetGeometry &geometry, const ThreatSettings &settings) {
  ThreatAssessment assessment{};
  const RelativePath path(geometry);
  float distance, range_rate;
  const float time = approach_time(geometry, path, distance, range_rate);
  assessment.score = score(geometry, time, distance, range_rate);
  assessment.approach_time = time;
  assessment.approach_distance = distance;
  assessment.approach_altitude = geometry.altitude + geometry.vertical_speed * time;
  assessment.proximity_time = proximity_time(geometry, path, settings);
  // A target already in close proximity but moving away has passed, so it no longer raises the alarm.
  assessment.alarm = assessment.proximity_time >= 0.0f && assessment.proximity_time <= settings.alarm_time &&
                     !(assessment.proximity_time == 0.0f && range_rate > 0.0f);
  const float range = sqrtf(geometry.east * geometry.east + geometry.north * geometry.north);
  assessment.ignored = !assessment.alarm &&
                       (range > settings.max_distance || fabsf(geometry.altitude) > settings.max_altitude_difference);
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
