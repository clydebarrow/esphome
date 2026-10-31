#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "esphome/core/helpers.h"
#include "gdl90.h"

namespace esphome::skyecho {

/// Where a target is and how it is moving, relative to the ownship. Distances in
/// meters, speeds in meters per second, turn rates in radians per second, positive
/// to the right.
struct TargetGeometry {
  float north;           // target position north of the ownship
  float east;            // target position east of the ownship
  float altitude;        // target altitude above the ownship
  float velocity_north;  // target velocity relative to the ownship
  float velocity_east;
  float vertical_speed;      // target vertical speed relative to the ownship
  float own_velocity_north;  // ownship velocity over the ground, needed to follow its turn
  float own_velocity_east;
  float turn_rate;      // target turn rate
  float own_turn_rate;  // ownship turn rate
  bool gliders;         // both the ownship and the target are gliders or hang gliders
};

/// Thresholds for the alarm and for ignoring targets. Distances in meters, times in seconds.
struct ThreatSettings {
  // Close proximity: within this distance horizontally and this height vertically.
  float alarm_distance{926.0f};  // 0.5 NM
  float alarm_altitude{152.4f};  // 500 ft
  // The height used instead when both aircraft are gliders, which often fly close
  // together and climb and descend slowly.
  float glider_alarm_altitude{61.0f};  // 200 ft
  // Raise the alarm this long before a target is predicted to reach close proximity.
  float alarm_time{30.0f};
  // Targets further away than these are ignored, unless they raise the alarm.
  float max_distance{INFINITY};
  float max_altitude_difference{INFINITY};
};

/// What is known about one target after assessing it.
struct ThreatAssessment {
  bool ignored;             // outside the distance or altitude filter
  bool alarm;               // predicted to reach close proximity within the alarm time, and not moving away
  float score;              // how much attention it needs: lower is more significant
  float approach_time;      // seconds until the closest approach, zero when not closing
  float approach_distance;  // horizontal separation at the closest approach
  float approach_altitude;  // target height above the ownship at the closest approach, allowing for climb
  float proximity_time;     // seconds until close proximity, negative if not predicted
};

/// The most significant target at an assessment.
struct Threat {
  bool valid;                    // false without an ownship position, or when every target is ignored
  gdl90PositionReport_t report;  // the target, dead-reckoned to the time of the assessment
  ThreatAssessment assessment;
  float north;  // target offset from the ownship, in meters
  float east;
  float altitude;  // target height above the ownship, in meters
};

// Cap on how far a position is dead-reckoned, so stale targets do not run away.
static constexpr float MAX_EXTRAPOLATE_S = 2.0f;

/// Move a position report forward along its track, and round its turn, by dt seconds. Only the horizontal
/// position changes; the altitude is left at its last reported value.
void extrapolate_report(gdl90PositionReport_t &report, float dt);

/**
 * Estimate the turn rate of an aircraft, in degrees per second, from the change in
 * track between its previous report and a new one received dt seconds later. The
 * result is smoothed with the previous estimate, held in previous.turnRate, since
 * the track is only sent in steps of 1.4 degrees. It is zero when the track is not
 * valid, the aircraft is moving too slowly for its track to mean much, or the
 * reports are too far apart.
 */
float estimate_turn_rate(const gdl90PositionReport_t &previous, const gdl90PositionReport_t &current, float dt);

/// Work out the geometry of a target relative to the ownship. A track or vertical
/// speed that is not valid counts as no movement in that direction. A turn rate
/// below 1.5 degrees per second counts as flying straight.
TargetGeometry target_geometry(const gdl90PositionReport_t &ownship, const gdl90PositionReport_t &target);

/**
 * Score how much attention a target needs: lower is more significant.
 *
 * The score is the separation at the closest point of approach within a short
 * look-ahead, with height separation weighted so that 1000 ft counts the same as
 * 1 NM horizontally. Both aircraft are assumed to keep their speed, vertical speed
 * and turn rate. The closest approach is when the target is nearest horizontally,
 * with the height difference then predicted from the vertical speeds; a target
 * with no horizontal movement relative to the ownship is closest when it passes
 * the ownship's height. A target that is moving away is scored as further, up to
 * twice as far when it is heading directly away and will get no closer.
 */
float threat_score(const TargetGeometry &geometry);

/// Assess one target: its score, its closest approach and whether it raises the alarm.
ThreatAssessment assess_threat(const TargetGeometry &geometry, const ThreatSettings &settings);

/// Picks the most significant of a set of targets, one at a time. A target that
/// raises the alarm comes before any that does not, the soonest one first.
class SignificantTargetSelector {
 public:
  SignificantTargetSelector(const gdl90PositionReport_t &ownship, const ThreatSettings &settings)
      : ownship_(ownship), settings_(settings) {}

  /// Assess a target, remember it if it is the most significant so far, and return the assessment.
  ThreatAssessment consider(size_t index, const gdl90PositionReport_t &target);

  /// The index of the most significant target, or -1 when none was considered or all were ignored.
  int get_best() const { return this->best_; }
  /// The assessment of the most significant target; only meaningful when get_best() is not -1.
  const ThreatAssessment &get_best_assessment() const { return this->best_assessment_; }

 protected:
  const gdl90PositionReport_t &ownship_;
  const ThreatSettings &settings_;
  int best_{-1};
  ThreatAssessment best_assessment_{};
};

/// Says when a threat reaches each of a set of warning times before close proximity: once per time, and for a
/// threat first seen after some of the times have passed, once for the shortest of those.
class ThreatWarnings {
 public:
  /// Seconds before close proximity, longest first.
  void set_times(std::initializer_list<uint16_t> seconds) { this->times_ = seconds; }
  /// Check the latest assessment of the most significant target. Returns the warning time it has reached, or 0
  /// when no warning is due.
  uint16_t check(const Threat &threat);

 protected:
  FixedVector<uint16_t> times_{};
  uint32_t address_{0};  // the threat being warned about, or zero for none
  size_t next_{0};       // index of the next warning time for that threat
};

}  // namespace esphome::skyecho
