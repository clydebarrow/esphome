#include "flight_detector.h"

#include <algorithm>

namespace esphome::tug_logger {

FlightEvent FlightDetector::update(const PositionSample &sample) {
  if (this->flying_ && this->has_last_sample_ &&
      sample.now_ms - this->last_sample_ms_ > this->settings_.max_data_gap_ms)
    this->flight_.approximate = true;
  this->last_sample_ms_ = sample.now_ms;
  this->has_last_sample_ = true;

  if (this->flying_) {
    if (this->flight_.launch == 0 && sample.epoch != 0)
      this->flight_.launch = sample.epoch - time_t((sample.now_ms - this->launch_ms_) / 1000);
    this->track_flight_(sample);
    this->flight_.elapsed_s = (sample.now_ms - this->launch_ms_) / 1000;
  } else {
    // The reference follows the aircraft on the ground, and stops as it accelerates for takeoff
    if (sample.speed_valid && sample.ground_speed_mps < this->settings_.landing_speed_mps) {
      if (sample.geometric_valid)
        this->ground_geometric_.add(sample.now_ms, sample.geometric_m);
      if (sample.pressure_valid)
        this->ground_pressure_.add(sample.now_ms, sample.pressure_m);
    }
  }

  // Without a speed, neither state changes and a run in progress carries on
  if (!sample.speed_valid)
    return FlightEvent::FLIGHT_EVENT_NONE;

  const bool past_threshold = this->flying_ ? sample.ground_speed_mps < this->settings_.landing_speed_mps
                                            : sample.ground_speed_mps >= this->settings_.takeoff_speed_mps;
  if (!past_threshold) {
    this->crossing_ = false;
    return FlightEvent::FLIGHT_EVENT_NONE;
  }
  if (!this->crossing_) {
    this->crossing_ = true;
    this->crossing_ms_ = sample.now_ms;
    this->crossing_epoch_ = sample.epoch;
  }
  const uint32_t held_ms = sample.now_ms - this->crossing_ms_;
  if (!this->flying_) {
    if (held_ms < this->settings_.takeoff_confirm_ms)
      return FlightEvent::FLIGHT_EVENT_NONE;
    this->start_flight_(sample);
    return FlightEvent::FLIGHT_EVENT_TAKEOFF;
  }
  if (held_ms < this->settings_.landing_confirm_ms)
    return FlightEvent::FLIGHT_EVENT_NONE;
  // The flight ended when the aircraft first slowed, not when that was confirmed
  this->flight_.elapsed_s = (this->crossing_ms_ - this->launch_ms_) / 1000;
  this->flight_.climb_s = std::min(this->flight_.climb_s, this->flight_.elapsed_s);
  this->flying_ = false;
  this->crossing_ = false;
  return FlightEvent::FLIGHT_EVENT_LANDED;
}

void FlightDetector::start_flight_(const PositionSample &sample) {
  this->flight_ = FlightSnapshot{};
  this->flying_ = true;
  this->crossing_ = false;
  this->launch_ms_ = this->crossing_ms_;
  this->height_count_ = 0;
  this->current_height_ft_ = 0;
  if (this->crossing_epoch_ != 0) {
    this->flight_.launch = this->crossing_epoch_;
  } else if (sample.epoch != 0) {
    this->flight_.launch = sample.epoch - time_t((sample.now_ms - this->launch_ms_) / 1000);
  }
  // Ages are measured from the start of the run: the reference stops updating there. Pressure altitude first: it
  // holds steady on the ground, where GPS altitude wanders
  const uint32_t max_age = this->settings_.max_reference_age_ms;
  const uint32_t window = this->settings_.reference_window_ms;
  if (this->ground_pressure_.mean(this->launch_ms_, window, max_age, this->flight_.reference_m)) {
    this->flight_.kind = AltitudeKind::ALTITUDE_KIND_PRESSURE;
    this->flight_.has_reference = true;
  } else if (this->ground_geometric_.mean(this->launch_ms_, window, max_age, this->flight_.reference_m)) {
    this->flight_.kind = AltitudeKind::ALTITUDE_KIND_GEOMETRIC;
    this->flight_.has_reference = true;
  } else {
    this->flight_.approximate = true;
  }
  this->ground_geometric_.clear();
  this->ground_pressure_.clear();
  this->track_flight_(sample);
  this->flight_.elapsed_s = (sample.now_ms - this->launch_ms_) / 1000;
}

bool FlightDetector::altitude_for_(const PositionSample &sample, float &altitude_m) const {
  switch (this->flight_.kind) {
    case AltitudeKind::ALTITUDE_KIND_GEOMETRIC:
      altitude_m = sample.geometric_m;
      return sample.geometric_valid;
    case AltitudeKind::ALTITUDE_KIND_PRESSURE:
      altitude_m = sample.pressure_m;
      return sample.pressure_valid;
    default:
      return false;
  }
}

void FlightDetector::track_flight_(const PositionSample &sample) {
  // With no reference, the first altitude seen in flight decides the kind, and heights use the field elevation
  if (this->flight_.kind == AltitudeKind::ALTITUDE_KIND_NONE) {
    if (sample.pressure_valid) {
      this->flight_.kind = AltitudeKind::ALTITUDE_KIND_PRESSURE;
    } else if (sample.geometric_valid) {
      this->flight_.kind = AltitudeKind::ALTITUDE_KIND_GEOMETRIC;
    } else {
      return;
    }
  }
  float altitude_m;
  if (!this->altitude_for_(sample, altitude_m))
    return;
  const float altitude_ft = altitude_m / METERS_PER_FOOT;
  const float zero_ft =
      this->flight_.has_reference ? this->flight_.reference_m / METERS_PER_FOOT : this->settings_.field_elevation_ft;
  const float height_ft = this->median_height_(altitude_ft - zero_ft);
  this->current_height_ft_ = int32_t(height_ft + (height_ft < 0 ? -0.5f : 0.5f));
  if (this->current_height_ft_ > this->flight_.max_height_ft) {
    this->flight_.max_height_ft = this->current_height_ft_;
    this->flight_.climb_s = (sample.now_ms - this->launch_ms_) / 1000;
  }
}

void GroundAltitudes::add(uint32_t now_ms, float altitude_m) {
  this->altitudes_[this->next_] = altitude_m;
  this->times_[this->next_] = now_ms;
  this->next_ = (this->next_ + 1) % SIZE;
  if (this->count_ != SIZE)
    this->count_++;
}

bool GroundAltitudes::mean(uint32_t now_ms, uint32_t window_ms, uint32_t max_age_ms, float &mean_m) const {
  if (this->count_ == 0)
    return false;
  const uint32_t newest = this->times_[(this->next_ + SIZE - 1) % SIZE];
  if (now_ms - newest > max_age_ms)
    return false;
  float sum = 0.0f;
  unsigned used = 0;
  for (uint8_t i = 1; i <= this->count_; i++) {
    const uint8_t index = (this->next_ + SIZE - i) % SIZE;
    if (newest - this->times_[index] >= window_ms)
      break;
    sum += this->altitudes_[index];
    used++;
  }
  mean_m = sum / float(used);
  return true;
}

float FlightDetector::median_height_(float height_ft) {
  if (this->height_count_ < 3) {
    this->heights_[this->height_count_++] = height_ft;
  } else {
    this->heights_[0] = this->heights_[1];
    this->heights_[1] = this->heights_[2];
    this->heights_[2] = height_ft;
  }
  // Until there are three, the lowest: a bad first fix must not set the peak
  if (this->height_count_ < 3)
    return *std::min_element(this->heights_, this->heights_ + this->height_count_);
  float sorted[3] = {this->heights_[0], this->heights_[1], this->heights_[2]};
  std::sort(sorted, sorted + 3);
  return sorted[1];
}

void FlightDetector::resume(const FlightSnapshot &snapshot, uint32_t now_ms) {
  this->flight_ = snapshot;
  this->flying_ = true;
  this->crossing_ = false;
  this->height_count_ = 0;
  this->launch_ms_ = now_ms - snapshot.elapsed_s * 1000;
  this->last_sample_ms_ = now_ms;
  this->has_last_sample_ = true;
}

}  // namespace esphome::tug_logger
