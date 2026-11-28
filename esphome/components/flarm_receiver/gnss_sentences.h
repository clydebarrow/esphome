#pragma once

#include <cstdint>
#include <cstdio>

#include "esphome/components/gps/nmea.h"

namespace esphome::flarm_receiver {

/// A value in thousandths as NMEA writes it, to 2 decimals and truncated: -12345 gives "-12.34"
inline void nmea_thousandths(int32_t value_e3, char *out, size_t size) {
  const uint32_t magnitude = value_e3 < 0 ? uint32_t(-int64_t(value_e3)) : uint32_t(value_e3);
  snprintf(out, size, "%s%u.%02u", value_e3 < 0 ? "-" : "", unsigned(magnitude / 1000u),
           unsigned(magnitude % 1000u / 10u));
}

/// A coordinate as NMEA writes it: degrees and minutes to 5 decimals, then the hemisphere letter
inline void nmea_coordinate(int32_t value_e7, unsigned degree_digits, char positive, char negative, char *out,
                            size_t size) {
  const uint32_t magnitude = value_e7 < 0 ? uint32_t(-int64_t(value_e7)) : uint32_t(value_e7);
  // Rounded to the nearest 0.00001 minute; the largest remainder gives 59.99999
  const uint32_t minutes_e5 = (magnitude % 10000000u * 60u + 50u) / 100u;
  snprintf(out, size, "%0*u%02u.%05u,%c", int(degree_digits), unsigned(magnitude / 10000000u),
           unsigned(minutes_e5 / 100000u), unsigned(minutes_e5 % 100000u), value_e7 < 0 ? negative : positive);
}

/// The time as hhmmss.cc, or empty if there is none
inline void nmea_time(const gps::GPSData &data, char *out, size_t size) {
  if (!data.has(gps::GPS_FIELD_TIME)) {
    if (size != 0)
      out[0] = '\0';
    return;
  }
  unsigned centiseconds = 0;
#ifdef USE_GPS_MILLISECOND
  centiseconds = data.millisecond / 10u;
#endif
  snprintf(out, size, "%02u%02u%02u.%02u", data.hour, data.minute, data.second, centiseconds);
}

/// The body of a GPRMC sentence (no '$' or checksum) for a sentence that set the position; false otherwise
inline bool rmc_body(const gps::GPSData &data, char *out, size_t size) {
  if (!data.is_updated(gps::GPS_FIELD_LOCATION) || !data.is_updated(gps::GPS_FIELD_DATE))
    return false;
  char time[12], latitude[16], longitude[17];
  nmea_time(data, time, sizeof(time));
  nmea_coordinate(data.latitude_e7, 2, 'N', 'S', latitude, sizeof(latitude));
  nmea_coordinate(data.longitude_e7, 3, 'E', 'W', longitude, sizeof(longitude));
  snprintf(out, size, "GPRMC,%s,A,%s,%s,%u.%02u,%u.%02u,%02u%02u%02u,,,A", time, latitude, longitude,
           unsigned(data.speed_e3 / 1000u), unsigned(data.speed_e3 % 1000u / 10u), unsigned(data.course_e2 / 100u),
           unsigned(data.course_e2 % 100u), data.day, data.month, unsigned(data.year % 100u));
  return true;
}

/// The body of a GPGGA sentence for a GGA sentence just read, with or without a fix; false after another sentence
inline bool gga_body(const gps::GPSData &data, char *out, size_t size) {
  if (!data.is_updated(gps::GPS_FIELD_SATELLITES))
    return false;
  char time[12];
  nmea_time(data, time, sizeof(time));
  const unsigned hdop_e2 = data.hdop_e2;
  if (!data.is_updated(gps::GPS_FIELD_LOCATION)) {
    snprintf(out, size, "GPGGA,%s,,,,,0,%02u,%u.%02u,,M,,M,,", time, data.satellites, hdop_e2 / 100u, hdop_e2 % 100u);
    return true;
  }
  char latitude[16], longitude[17], altitude[16], separation[16] = "";
#ifdef USE_GPS_GEOID_SEPARATION
  if (data.has(gps::GPS_FIELD_GEOID_SEPARATION))
    nmea_thousandths(data.geoid_separation_e3, separation, sizeof(separation));
#endif
  nmea_thousandths(data.altitude_e3, altitude, sizeof(altitude));
  nmea_coordinate(data.latitude_e7, 2, 'N', 'S', latitude, sizeof(latitude));
  nmea_coordinate(data.longitude_e7, 3, 'E', 'W', longitude, sizeof(longitude));
  snprintf(out, size, "GPGGA,%s,%s,%s,1,%02u,%u.%02u,%s,M,%s,M,,", time, latitude, longitude, data.satellites,
           hdop_e2 / 100u, hdop_e2 % 100u, altitude, separation);
  return true;
}

}  // namespace esphome::flarm_receiver
