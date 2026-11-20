#pragma once

#include <cstdint>

#include "gdl90.h"

namespace esphome::skyecho {

/// An aircraft heard on FLARM by a receiver other than the SkyEcho, such as the tug's LoRa relay (flarm_receiver).
struct FlarmTarget {
  uint32_t address;
  bool icao;              // an ICAO address, as ADS-B reports use, not a FLARM ID
  uint8_t aircraft_type;  // FLARM's, 0 to 15
  bool no_track;
  float latitude;
  float longitude;
  float altitude_hae;  // meters above the WGS84 ellipsoid, as FLARM sends it
  float speed;         // meters per second
  float track;         // degrees true
  float climb_rate;    // meters per second
};

/// The GDL90 emitter category nearest to a FLARM aircraft type.
emitterCategory_t flarm_category(uint8_t aircraft_type);

/// A short callsign for a FLARM target: the registration of an Australian ICAO address ("GKP" for VH-GKP), or else
/// the address in hex. callsign holds GDL_CALLSIGN_LENGTH characters and the null.
void flarm_callsign(const FlarmTarget &target, char *callsign);

/**
 * A FLARM target as a GDL90 traffic report beside the ownship's. GDL90 reports give pressure altitudes, and FLARM a
 * height above the ellipsoid, so the target's altitude is moved by the ownship's difference between the two: its
 * pressure altitude, less its geometric altitude (own_geo_altitude, above the ellipsoid as GDL90 gives it). Without
 * that (has_geo_altitude false), the FLARM height is used as it is.
 */
gdl90PositionReport_t flarm_report(const FlarmTarget &target, const gdl90PositionReport_t &ownship,
                                   bool has_geo_altitude, float own_geo_altitude);

/**
 * Drops the FLARM of this aircraft itself, which another receiver hears where the ownship is. A target this close to
 * the ownship is dropped while it is, and one that stays this close for LEARN_MS is taken to be the ownship's own and
 * dropped from then on, wherever it seems to be.
 */
class OwnFlarmFilter {
 public:
  static constexpr float MAX_DISTANCE_M = 25.0f;
  static constexpr float MAX_ALTITUDE_M = 30.0f;
  static constexpr uint32_t LEARN_MS = 10000;

  /// True if the report, made at time now in ms, is the ownship's own FLARM.
  bool is_own(const gdl90PositionReport_t &ownship, const gdl90PositionReport_t &report, uint32_t now);
  /// The address taken as the ownship's, if one has been.
  bool get_own_address(uint32_t *address) const {
    *address = this->own_address_;
    return this->learned_;
  }

 protected:
  uint32_t own_address_{0};
  uint32_t candidate_address_{0};
  uint32_t candidate_since_ms_{0};
  uint32_t candidate_last_ms_{0};
  bool learned_{false};
  bool has_candidate_{false};
};

}  // namespace esphome::skyecho
