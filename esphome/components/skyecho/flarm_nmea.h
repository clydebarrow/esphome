#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "gdl90.h"
#include "threat.h"

namespace esphome::skyecho {

/// The ownship's GNSS fix, as a receiver such as TinyGPS gives it, for the GPRMC and GPGGA sentences.
struct NmeaFix {
  bool valid;
  double latitude;
  double longitude;
  float altitude;  // meters above mean sea level, as the receiver reports it
  float geoid_separation;
  float speed;   // meters per second
  float course;  // degrees true
  uint8_t satellites;
  float hdop;
  bool time_valid;
  uint8_t hour, minute, second, centisecond;
  bool date_valid;
  uint16_t year;
  uint8_t month, day;
};

/// What the PFLAU sentence reports about the device itself.
struct NmeaStatus {
  bool gps_fix;
  bool flying;
  bool transmitting;  // a FLARM transmitter; false for a receive-only device
};

/// Adds the NMEA checksum and line end to a sentence body given without the '$': "GPRMC,...".
std::string nmea_sentence(const std::string &body);

/// The FLARM aircraft type (0 to 15) nearest to a GDL90 emitter category.
uint8_t flarm_aircraft_type(emitterCategory_t category);

/**
 * The FLARM alarm level (0 to 3) for a target's assessment: 1, 2 and 3 when close proximity is predicted within 20,
 * 13 and 8 seconds, as FLARM's own levels are, and 0 otherwise.
 */
uint8_t flarm_alarm_level(const ThreatAssessment &assessment);

/// GPRMC and GPGGA from the fix; nothing without a valid one.
void append_gps_sentences(std::string &out, const NmeaFix &fix);

/// PGRMZ, the pressure altitude, as FLARM sends it from its barometer.
void append_pgrmz(std::string &out, float pressure_altitude_m);

/**
 * PFLAU and a PFLAA for each target, as a FLARM sends them each second. Targets are placed relative to the ownship,
 * and their altitudes must be on the same scale as its own. At most max_targets PFLAA are sent, the first in the
 * list, which should be nearest first. PFLAU gives the target with the highest alarm level, if any has one. On the
 * ground (status.flying false) no target has an alarm level.
 */
void append_flarm_traffic(std::string &out, const gdl90PositionReport_t &ownship, const gdl90PositionReport_t *targets,
                          size_t count, const ThreatSettings &settings, const NmeaStatus &status,
                          size_t max_targets = 15);

}  // namespace esphome::skyecho
