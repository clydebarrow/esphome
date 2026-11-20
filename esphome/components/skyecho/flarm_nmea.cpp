#include "flarm_nmea.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>

namespace esphome::skyecho {

static constexpr float MPS_TO_KNOTS = 3600.0f / 1852.0f;
static constexpr float METERS_TO_FEET = 1.0f / 0.3048f;
static constexpr float RAD_TO_DEG = 180.0f / static_cast<float>(M_PI);

std::string nmea_sentence(const std::string &body) {
  uint8_t sum = 0;
  for (char c : body)
    sum ^= static_cast<uint8_t>(c);
  char tail[6];
  snprintf(tail, sizeof(tail), "*%02X\r\n", sum);
  return "$" + body + tail;
}

uint8_t flarm_aircraft_type(emitterCategory_t category) {
  switch (category) {
    case Glider:
      return 1;
    case Rotorcraft:
      return 3;
    case Parachute:
      return 4;
    case HangGlider:
      return 6;
    case Light:
    case Small:
      return 8;
    case Large:
    case HighVortex:
    case Heavy:
    case Fighter:
      return 9;
    case Balloon:
      return 11;
    case UAV:
      return 13;
    case Obstacle:
    case ObstacleCluster:
    case ObstacleLine:
      return 15;
    default:
      return 0;
  }
}

uint8_t flarm_alarm_level(const ThreatAssessment &assessment) {
  if (!assessment.alarm || assessment.proximity_time < 0)
    return 0;
  if (assessment.proximity_time <= 8.0f)
    return 3;
  if (assessment.proximity_time <= 13.0f)
    return 2;
  if (assessment.proximity_time <= 20.0f)
    return 1;
  return 0;
}

// ddmm.mmmm or dddmm.mmmm, then the hemisphere
static void append_coordinate(std::string &out, double degrees, bool latitude) {
  const char hemisphere = latitude ? (degrees < 0 ? 'S' : 'N') : (degrees < 0 ? 'W' : 'E');
  double value = std::fabs(degrees);
  int whole = static_cast<int>(value);
  double minutes = (value - whole) * 60.0;
  // Rounding to 4 places can reach 60
  if (minutes >= 59.99995) {
    whole++;
    minutes = 0;
  }
  char buf[20];
  snprintf(buf, sizeof(buf), latitude ? "%02d%07.4f,%c" : "%03d%07.4f,%c", whole, minutes, hemisphere);
  out += buf;
}

static void append_time(std::string &out, const NmeaFix &fix) {
  if (!fix.time_valid)
    return;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02u%02u%02u.%02u", fix.hour, fix.minute, fix.second, fix.centisecond);
  out += buf;
}

void append_gps_sentences(std::string &out, const NmeaFix &fix) {
  if (!fix.valid)
    return;
  char buf[48];
  std::string rmc = "GPRMC,";
  append_time(rmc, fix);
  rmc += ",A,";
  append_coordinate(rmc, fix.latitude, true);
  rmc += ',';
  append_coordinate(rmc, fix.longitude, false);
  snprintf(buf, sizeof(buf), ",%.1f,%.1f,", fix.speed * MPS_TO_KNOTS, fix.course);
  rmc += buf;
  if (fix.date_valid) {
    snprintf(buf, sizeof(buf), "%02u%02u%02u", fix.day, fix.month, fix.year % 100);
    rmc += buf;
  }
  rmc += ",,,A";
  out += nmea_sentence(rmc);

  std::string gga = "GPGGA,";
  append_time(gga, fix);
  gga += ',';
  append_coordinate(gga, fix.latitude, true);
  gga += ',';
  append_coordinate(gga, fix.longitude, false);
  snprintf(buf, sizeof(buf), ",1,%02u,%.1f,%.1f,M,%.1f,M,,", fix.satellites, fix.hdop, fix.altitude,
           fix.geoid_separation);
  gga += buf;
  out += nmea_sentence(gga);
}

void append_pgrmz(std::string &out, float pressure_altitude_m) {
  char buf[32];
  snprintf(buf, sizeof(buf), "PGRMZ,%d,F,2", static_cast<int>(std::lround(pressure_altitude_m * METERS_TO_FEET)));
  out += nmea_sentence(buf);
}

static uint8_t id_type(emitterAddressType_t type) {
  switch (type) {
    case EmitterAdsbIcao:
    case EmitterTisbIcao:
      return 1;
    case EmitterFlarm:
      return 2;
    default:
      return 0;
  }
}

void append_flarm_traffic(std::string &out, const gdl90PositionReport_t &ownship, const gdl90PositionReport_t *targets,
                          size_t count, const ThreatSettings &settings, const NmeaStatus &status, size_t max_targets) {
  char buf[96];
  std::string traffic;
  uint8_t worst_level = 0;
  const gdl90PositionReport_t *worst = nullptr;
  float worst_north = 0, worst_east = 0, worst_vertical = 0;
  for (size_t i = 0; i != count && i != max_targets; i++) {
    const gdl90PositionReport_t &target = targets[i];
    const float north = trafficNorthing(&ownship, &target);
    const float east = trafficEasting(&ownship, &target);
    const float vertical = target.isAltitudeValid && ownship.isAltitudeValid ? target.altitude - ownship.altitude : 0;
    const uint8_t level = flarm_alarm_level(assess_threat(target_geometry(ownship, target), settings));
    if (level > worst_level) {
      worst_level = level;
      worst = &target;
      worst_north = north;
      worst_east = east;
      worst_vertical = vertical;
    }
    std::string line;
    snprintf(buf, sizeof(buf), "PFLAA,%u,%d,%d,%d,%u,%06" PRIX32 ",", level, static_cast<int>(std::lround(north)),
             static_cast<int>(std::lround(east)), static_cast<int>(std::lround(vertical)), id_type(target.addressType),
             target.address & 0xFFFFFF);
    line = buf;
    if (target.isTrackValid) {
      snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(target.track)) % 360);
      line += buf;
    }
    line += ",,";
    if (target.isGroundSpeedValid) {
      snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(target.groundSpeed)));
      line += buf;
    }
    line += ',';
    if (target.isVerticalSpeedValid) {
      snprintf(buf, sizeof(buf), "%.1f", target.verticalSpeed);
      line += buf;
    }
    snprintf(buf, sizeof(buf), ",%X", flarm_aircraft_type(target.category));
    line += buf;
    traffic += nmea_sentence(line);
  }

  const unsigned received = count < 99 ? static_cast<unsigned>(count) : 99;
  // No fix, a fix on the ground, or one in flight
  const unsigned gps = !status.gps_fix ? 0 : status.flying ? 2 : 1;
  if (worst != nullptr) {
    float bearing = std::atan2(worst_east, worst_north) * RAD_TO_DEG;
    if (ownship.isTrackValid)
      bearing -= ownship.track;
    bearing = std::remainder(bearing, 360.0f);
    const float distance = std::sqrt(worst_north * worst_north + worst_east * worst_east);
    snprintf(buf, sizeof(buf), "PFLAU,%u,%u,%u,1,%u,%d,2,%d,%u,%06" PRIX32, received, status.transmitting ? 1u : 0u,
             gps, worst_level, static_cast<int>(std::lround(bearing)), static_cast<int>(std::lround(worst_vertical)),
             static_cast<unsigned>(std::lround(distance)), worst->address & 0xFFFFFF);
  } else {
    snprintf(buf, sizeof(buf), "PFLAU,%u,%u,%u,1,0,,0,,,", received, status.transmitting ? 1u : 0u, gps);
  }
  out += nmea_sentence(buf);
  out += traffic;
}

}  // namespace esphome::skyecho
