#include "flight_log.h"

#include <cstring>

namespace esphome::glider_tracker {

// A clock that isn't set yet: times made up from this still differ from one fake flight to the next
static constexpr uint32_t MADE_UP_TIME = 1800000000;
static constexpr uint32_t FAKE_FLIGHT_S = 3600;

void FlightLog::clear_() {
  memset(&this->store_, 0, sizeof(this->store_));
  this->store_.magic = FlightStore::MAGIC;
}

void FlightLog::load(const FlightStore &store) {
  if (store.magic != FlightStore::MAGIC) {
    this->clear_();
    this->changed_ = true;
    return;
  }
  this->store_ = store;
  this->takeoff_pending_ = this->is_flying();
}

bool FlightLog::get_takeoff_report(Takeoff *report) const {
  Summary flight;
  if (!this->takeoff_pending_ || !this->get_flight(&flight))
    return false;
  report->takeoff = flight.takeoff;
  report->lat = flight.takeoff_lat;
  report->lon = flight.takeoff_lon;
  report->alt_m = flight.takeoff_alt_m;
  return true;
}

const uint8_t *FlightLog::next_summary() const {
  const uint8_t *found = nullptr;
  uint32_t oldest = UINT32_MAX;
  for (const auto &slot : this->store_.queue) {
    Summary queued;
    if (queued.decode(slot, SLOT) && queued.takeoff < oldest) {
      oldest = queued.takeoff;
      found = slot;
    }
  }
  return found;
}

size_t FlightLog::queued() const {
  size_t count = 0;
  for (const auto &slot : this->store_.queue)
    count += slot[0] == SUMMARY;
  return count;
}

bool FlightLog::acknowledge(const Ack &ack) {
  if (ack.type == TAKEOFF_ACK) {
    Summary flight;
    if (!this->takeoff_pending_ || !this->get_flight(&flight) || flight.takeoff != ack.takeoff)
      return false;
    this->takeoff_pending_ = false;
    return true;
  }
  for (auto &slot : this->store_.queue) {
    Summary queued;
    if (queued.decode(slot, SLOT) && queued.takeoff == ack.takeoff) {
      memset(slot, 0, SLOT);
      this->changed_ = true;
      return true;
    }
  }
  return false;
}

void FlightLog::enqueue_(const Summary &summary) {
  uint8_t *slot = nullptr;
  for (auto &queued_slot : this->store_.queue) {
    Summary queued;
    if (queued.decode(queued_slot, SLOT) && queued.takeoff == summary.takeoff) {
      slot = queued_slot;  // the same flight again: replace it
      break;
    }
    if (queued_slot[0] != SUMMARY && slot == nullptr)
      slot = queued_slot;
  }
  if (slot == nullptr) {
    // Full: drop the oldest, which the clubhouse has not answered for 8 flights
    slot = const_cast<uint8_t *>(this->next_summary());
  }
  summary.encode(slot);
  this->changed_ = true;
}

void FlightLog::take_off_(const Summary &candidate) {
  candidate.encode(this->store_.flight);
  this->takeoff_pending_ = true;
  this->slow_since_ = 0;
  this->changed_ = true;
}

void FlightLog::land_(const Summary &flight) {
  memset(this->store_.flight, 0, SLOT);
  this->slow_since_ = 0;
  // The summary says all the takeoff report did, and more
  this->takeoff_pending_ = false;
  this->fake_airborne_ = false;
  this->enqueue_(flight);
}

FlightLog::Event FlightLog::update(const Sample &sample) {
  if (sample.now == 0 || !sample.fix || !sample.speed_valid)
    return Event::NONE;
  const float speed = sample.speed_kmh;
  if (!this->is_flying()) {
    if (speed < this->settings_.takeoff_kmh) {
      this->fast_seconds_ = 0;
      return Event::NONE;
    }
    // The takeoff is where and when the run above the speed began
    if (this->fast_seconds_++ == 0) {
      this->candidate_ = Summary();
      this->candidate_.takeoff = sample.now;
      this->candidate_.takeoff_lat = sample.latitude;
      this->candidate_.takeoff_lon = sample.longitude;
      this->candidate_.takeoff_alt_m = sample.altitude_m;
    }
    if (this->fast_seconds_ < this->settings_.takeoff_seconds)
      return Event::NONE;
    this->fast_seconds_ = 0;
    this->take_off_(this->candidate_);
    return Event::TAKEOFF;
  }
  if (this->fake_airborne_)
    return Event::NONE;
  Summary flight;
  this->get_flight(&flight);
  if (flight.landing == 0) {
    if (speed < this->settings_.landed_kmh) {
      flight.landing = sample.now;
      flight.landing_lat = sample.latitude;
      flight.landing_lon = sample.longitude;
      flight.landing_alt_m = sample.altitude_m;
      flight.encode(this->store_.flight);
      this->slow_since_ = sample.now;
      this->changed_ = true;
    }
    return Event::NONE;
  }
  if (speed > this->settings_.moving_again_kmh) {
    // Still rolling, or a touch-and-go
    flight.landing = 0;
    flight.landing_lat = flight.landing_lon = 0;
    flight.landing_alt_m = 0;
    flight.encode(this->store_.flight);
    this->slow_since_ = 0;
    this->changed_ = true;
    return Event::NONE;
  }
  if (this->slow_since_ == 0)
    this->slow_since_ = flight.landing;  // after a restart
  if (sample.now - this->slow_since_ < this->settings_.landed_seconds)
    return Event::NONE;
  this->land_(flight);
  return Event::LANDED;
}

void FlightLog::fake_flight(const Sample &sample, uint32_t uptime_s) {
  Summary summary;
  summary.landing = sample.now != 0 ? sample.now : MADE_UP_TIME + uptime_s;
  summary.takeoff = summary.landing - FAKE_FLIGHT_S;
  if (sample.fix) {
    summary.takeoff_lat = summary.landing_lat = sample.latitude;
    summary.takeoff_lon = summary.landing_lon = sample.longitude;
    summary.takeoff_alt_m = summary.landing_alt_m = sample.altitude_m;
  }
  this->enqueue_(summary);
}

FlightLog::Event FlightLog::fake_takeoff_or_landing(const Sample &sample, uint32_t uptime_s) {
  const uint32_t now = sample.now != 0 ? sample.now : MADE_UP_TIME + uptime_s;
  if (this->is_flying()) {
    Summary flight;
    this->get_flight(&flight);
    if (flight.landing == 0) {
      flight.landing = now;
      if (sample.fix) {
        flight.landing_lat = sample.latitude;
        flight.landing_lon = sample.longitude;
        flight.landing_alt_m = sample.altitude_m;
      }
    }
    this->land_(flight);
    return Event::LANDED;
  }
  Summary candidate;
  candidate.takeoff = now;
  if (sample.fix) {
    candidate.takeoff_lat = sample.latitude;
    candidate.takeoff_lon = sample.longitude;
    candidate.takeoff_alt_m = sample.altitude_m;
  }
  this->fake_airborne_ = true;
  this->take_off_(candidate);
  return Event::TAKEOFF;
}

}  // namespace esphome::glider_tracker
