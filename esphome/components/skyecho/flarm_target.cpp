#include "flarm_target.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome::skyecho {

// Australian ICAO addresses: 7C0000 plus the three characters after VH- in base 36, A to Z then 0 to 9
static constexpr uint32_t AUSTRALIAN_ADDRESS_BASE = 0x7C0000;
static constexpr uint32_t AUSTRALIAN_ADDRESS_COUNT = 36 * 36 * 36;
static constexpr char REGISTRATION_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
// A report gap longer than this starts a new wait before a close target is taken as the ownship's
static constexpr uint32_t CANDIDATE_GAP_MS = 3000;
// Faster than this, a target is moving, as a GDL90 report would say it is in flight
static constexpr float IN_FLIGHT_SPEED = 10.0f;

emitterCategory_t flarm_category(uint8_t aircraft_type) {
  switch (aircraft_type) {
    case 1:  // glider
      return Glider;
    case 2:  // tow plane
    case 5:  // drop plane
    case 8:  // powered aircraft
      return Light;
    case 3:  // helicopter
      return Rotorcraft;
    case 4:  // skydiver
      return Parachute;
    case 6:  // hang glider
    case 7:  // paraglider
      return HangGlider;
    case 9:  // jet
      return Large;
    case 11:  // balloon
    case 12:  // airship
      return Balloon;
    case 13:
      return UAV;
    case 15:
      return Obstacle;
    default:
      return None;
  }
}

void flarm_callsign(const FlarmTarget &target, char *callsign) {
  if (target.icao && target.address >= AUSTRALIAN_ADDRESS_BASE &&
      target.address < AUSTRALIAN_ADDRESS_BASE + AUSTRALIAN_ADDRESS_COUNT) {
    uint32_t value = target.address - AUSTRALIAN_ADDRESS_BASE;
    callsign[3] = 0;
    for (int i = 2; i >= 0; i--) {
      callsign[i] = REGISTRATION_CHARS[value % 36];
      value /= 36;
    }
    return;
  }
  snprintf(callsign, GDL_CALLSIGN_LENGTH + 1, "%06" PRIX32, target.address & 0xFFFFFF);
}

gdl90PositionReport_t flarm_report(const FlarmTarget &target, const gdl90PositionReport_t &ownship,
                                   bool has_geo_altitude, float own_geo_altitude) {
  gdl90PositionReport_t report;
  memset(&report, 0, sizeof(report));
  report.address = target.address;
  report.addressType = target.icao ? EmitterAdsbIcao : EmitterFlarm;
  report.category = flarm_category(target.aircraft_type);
  flarm_callsign(target, report.callsign);
  report.latitude = target.latitude;
  report.longitude = target.longitude;
  report.altitude = target.altitude_hae;
  if (has_geo_altitude && ownship.isAltitudeValid)
    report.altitude += ownship.altitude - own_geo_altitude;
  report.groundSpeed = target.speed;
  report.track = target.track;
  report.verticalSpeed = target.climb_rate;
  report.isAltitudeValid = true;
  report.isGroundSpeedValid = true;
  report.isTrackValid = true;
  report.isVerticalSpeedValid = true;
  report.isInFlight = target.speed > IN_FLIGHT_SPEED;
  return report;
}

bool OwnFlarmFilter::is_own(const gdl90PositionReport_t &ownship, const gdl90PositionReport_t &report, uint32_t now) {
  if (this->learned_ && report.address == this->own_address_)
    return true;
  const float distance = trafficDistance(&ownship, &report);
  const bool close = distance <= MAX_DISTANCE_M && ownship.isAltitudeValid &&
                     std::fabs(report.altitude - ownship.altitude) <= MAX_ALTITUDE_M;
  if (!close) {
    if (this->has_candidate_ && report.address == this->candidate_address_)
      this->has_candidate_ = false;
    return false;
  }
  if (this->learned_)
    return true;
  if (!this->has_candidate_ || report.address != this->candidate_address_ ||
      now - this->candidate_last_ms_ > CANDIDATE_GAP_MS) {
    this->has_candidate_ = true;
    this->candidate_address_ = report.address;
    this->candidate_since_ms_ = now;
  }
  this->candidate_last_ms_ = now;
  if (now - this->candidate_since_ms_ >= LEARN_MS) {
    this->learned_ = true;
    this->own_address_ = report.address;
  }
  return true;
}

}  // namespace esphome::skyecho
