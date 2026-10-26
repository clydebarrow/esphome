#include "flarm.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cstdlib>

namespace esphome {
namespace skyecho {

static const char *const TAG = "flarm";

void FlarmParser::process_bytes(const uint8_t *data, size_t len) {
  for (size_t i = 0; i != len; i++) {
    char c = static_cast<char>(data[i]);

    if (c == '$') {
      // Start of new sentence
      this->line_pos_ = 0;
      this->line_buffer_[this->line_pos_++] = c;
    } else if (c == '\r' || c == '\n') {
      // End of sentence
      if (this->line_pos_ > 0) {
        this->line_buffer_[this->line_pos_] = '\0';
        this->process_line_();
        this->line_pos_ = 0;
      }
    } else if (this->line_pos_ < sizeof(this->line_buffer_) - 1) {
      this->line_buffer_[this->line_pos_++] = c;
    }
  }
}

void FlarmParser::process_line_() {
  if (this->line_pos_ < 10) {
    return;  // Too short to be valid
  }

  const char *sentence = this->line_buffer_;

  // Verify it starts with $
  if (sentence[0] != '$') {
    return;
  }

  // Verify checksum
  if (!this->verify_checksum(sentence, this->line_pos_)) {
    ESP_LOGV(TAG, "Checksum failed for: %s", sentence);
    return;
  }

  // Check sentence type - FLARM proprietary sentences
  if (strncmp(sentence + 1, "PFLAU,", 6) == 0) {
    if (this->parse_pflau_(sentence + 7)) {
      ESP_LOGV(TAG, "Parsed PFLAU");
    }
  } else if (strncmp(sentence + 1, "PFLAA,", 6) == 0) {
    if (this->parse_pflaa_(sentence + 7)) {
      ESP_LOGV(TAG, "Parsed PFLAA");
    }
  }
  // GPS sentences for ownship position
  else if (strncmp(sentence + 1, "GPRMC,", 6) == 0 || strncmp(sentence + 1, "GNRMC,", 6) == 0) {
    if (this->parse_gprmc_(sentence + 7)) {
      ESP_LOGV(TAG, "Parsed GPRMC/GNRMC");
    }
  } else if (strncmp(sentence + 1, "GPGGA,", 6) == 0 || strncmp(sentence + 1, "GNGGA,", 6) == 0) {
    if (this->parse_gpgga_(sentence + 7)) {
      ESP_LOGV(TAG, "Parsed GPGGA/GNGGA");
    }
  } else if (strncmp(sentence + 1, "PGRMZ,", 6) == 0) {
    if (this->parse_pgrmz_(sentence + 7)) {
      ESP_LOGV(TAG, "Parsed PGRMZ");
    }
  }
}

bool FlarmParser::verify_checksum(const char *sentence, size_t len) {
  // Find the asterisk
  const char *asterisk = strchr(sentence, '*');
  if (asterisk == nullptr || asterisk - sentence < 2) {
    return false;
  }

  // Calculate checksum (XOR of all chars between $ and *)
  uint8_t checksum = 0;
  for (const char *p = sentence + 1; p < asterisk; p++) {
    checksum ^= static_cast<uint8_t>(*p);
  }

  // Parse expected checksum
  char *end;
  uint8_t expected = static_cast<uint8_t>(strtoul(asterisk + 1, &end, 16));

  return checksum == expected;
}

bool FlarmParser::parse_int(const char **pos, int32_t *out) {
  const char *p = *pos;

  // Skip to value or comma
  while (*p == ' ')
    p++;

  if (*p == ',' || *p == '*' || *p == '\0') {
    return false;  // Empty field
  }

  char *end;
  *out = strtol(p, &end, 10);
  *pos = end;

  // Skip to after comma
  while (**pos != ',' && **pos != '*' && **pos != '\0')
    (*pos)++;
  if (**pos == ',')
    (*pos)++;

  return true;
}

static bool parse_hex(const char **pos, uint32_t *out) {
  const char *p = *pos;

  // Skip to value or comma
  while (*p == ' ')
    p++;

  if (*p == ',' || *p == '*' || *p == '\0') {
    return false;  // Empty field
  }

  char *end;
  *out = strtoul(p, &end, 16);
  *pos = end;

  // Skip to after comma
  while (**pos != ',' && **pos != '*' && **pos != '\0')
    (*pos)++;
  if (**pos == ',')
    (*pos)++;

  return true;
}

bool FlarmParser::parse_float(const char **pos, float *out) {
  const char *p = *pos;

  // Skip to value or comma
  while (*p == ' ')
    p++;

  if (*p == ',' || *p == '*' || *p == '\0') {
    return false;  // Empty field
  }

  char *end;
  *out = strtof(p, &end);
  *pos = end;

  // Skip to after comma
  while (**pos != ',' && **pos != '*' && **pos != '\0')
    (*pos)++;
  if (**pos == ',')
    (*pos)++;

  return true;
}

// Parse NMEA latitude: DDMM.MMMMM,N/S -> decimal degrees
bool FlarmParser::parse_latitude(const char **pos, float *out) {
  float raw;
  if (!parse_float(pos, &raw)) {
    skip_field(pos);  // Skip N/S indicator
    return false;
  }

  // Get N/S indicator
  char ns = **pos;
  skip_field(pos);

  if (ns != 'N' && ns != 'S') {
    return false;
  }

  // Convert DDMM.MMMMM to decimal degrees
  int degrees = static_cast<int>(raw / 100);
  float minutes = raw - (degrees * 100);
  *out = degrees + (minutes / 60.0f);

  if (ns == 'S') {
    *out = -*out;
  }

  return true;
}

// Parse NMEA longitude: DDDMM.MMMMM,E/W -> decimal degrees
bool FlarmParser::parse_longitude(const char **pos, float *out) {
  float raw;
  if (!parse_float(pos, &raw)) {
    skip_field(pos);  // Skip E/W indicator
    return false;
  }

  // Get E/W indicator
  char ew = **pos;
  skip_field(pos);

  if (ew != 'E' && ew != 'W') {
    return false;
  }

  // Convert DDDMM.MMMMM to decimal degrees
  int degrees = static_cast<int>(raw / 100);
  float minutes = raw - (degrees * 100);
  *out = degrees + (minutes / 60.0f);

  if (ew == 'W') {
    *out = -*out;
  }

  return true;
}

bool FlarmParser::skip_field(const char **pos) {
  while (**pos != ',' && **pos != '*' && **pos != '\0')
    (*pos)++;
  if (**pos == ',') {
    (*pos)++;
    return true;
  }
  return false;
}

const char *FlarmParser::get_field(const char **pos, char *buf, size_t buf_size) {
  size_t len = 0;

  while (**pos != ',' && **pos != '*' && **pos != '\0' && len < buf_size - 1) {
    buf[len++] = **pos;
    (*pos)++;
  }
  buf[len] = '\0';

  if (**pos == ',')
    (*pos)++;

  return len > 0 ? buf : nullptr;
}

// Parse PFLAU sentence:
// $PFLAU,<Rx>,<Tx>,<GPS>,<Power>,<AlarmLevel>,<RelativeBearing>,<AlarmType>,<RelativeVertical>,<RelativeDistance>,<ID>*<checksum>
bool FlarmParser::parse_pflau_(const char *sentence) const {
  FlarmPflau data{};
  const char *pos = sentence;
  int32_t val;

  // Rx - number of received devices
  if (!parse_int(&pos, &val))
    return false;
  data.rx = static_cast<uint8_t>(val);

  // Tx - transmission status
  if (!parse_int(&pos, &val))
    return false;
  data.tx = static_cast<uint8_t>(val);

  // GPS status
  if (!parse_int(&pos, &val))
    return false;
  data.gps = static_cast<FlarmGpsStatus>(val);

  // Power status
  if (!parse_int(&pos, &val))
    return false;
  data.power = static_cast<FlarmPowerStatus>(val);

  // Alarm level
  if (!parse_int(&pos, &val))
    return false;
  data.alarm_level = static_cast<FlarmAlarmLevel>(val);

  // Relative bearing (optional)
  data.bearing_valid = parse_int(&pos, &val);
  if (data.bearing_valid) {
    data.relative_bearing = static_cast<int16_t>(val);
  } else {
    skip_field(&pos);
  }

  // Alarm type
  if (!parse_int(&pos, &val))
    return false;
  data.alarm_type = static_cast<FlarmAlarmType>(val);

  // Relative vertical (optional)
  data.vertical_valid = parse_int(&pos, &val);
  if (data.vertical_valid) {
    data.relative_vertical = val;
  } else {
    skip_field(&pos);
  }

  // Relative distance (optional)
  data.distance_valid = parse_int(&pos, &val);
  if (data.distance_valid) {
    data.relative_distance = static_cast<uint32_t>(val);
  } else {
    skip_field(&pos);
  }

  // ID (optional, hex)
  uint32_t hex_val;
  data.id_valid = parse_hex(&pos, &hex_val);
  if (data.id_valid) {
    data.id = hex_val;
  }

  if (this->pflau_callback_) {
    this->pflau_callback_(data);
  }

  return true;
}

// Parse PFLAA sentence:
// $PFLAA,<AlarmLevel>,<RelativeNorth>,<RelativeEast>,<RelativeVertical>,<IDType>,<ID>,<Track>,<TurnRate>,<GroundSpeed>,<ClimbRate>,<AircraftType>*<checksum>
bool FlarmParser::parse_pflaa_(const char *sentence) const {
  FlarmPflaa data{};
  const char *pos = sentence;
  int32_t val;
  uint32_t hex_val;

  // Alarm level
  if (!parse_int(&pos, &val))
    return false;
  data.alarm_level = static_cast<FlarmAlarmLevel>(val);

  // Relative North (required)
  if (!parse_int(&pos, &val))
    return false;
  data.relative_north = val;

  // Relative East (optional - if empty, RelativeNorth is distance to non-directional target)
  data.east_valid = parse_int(&pos, &val);
  if (data.east_valid) {
    data.relative_east = val;
  } else {
    skip_field(&pos);
  }

  // Relative Vertical (required)
  if (!parse_int(&pos, &val))
    return false;
  data.relative_vertical = val;

  // ID Type
  if (!parse_int(&pos, &val))
    return false;
  data.id_type = static_cast<FlarmIdType>(val);

  // ID (hex)
  if (!parse_hex(&pos, &hex_val))
    return false;
  data.id = hex_val;

  // Track (optional)
  data.track_valid = parse_int(&pos, &val);
  if (data.track_valid) {
    data.track = static_cast<int16_t>(val);
  } else {
    skip_field(&pos);
  }

  // Turn rate (reserved, skip)
  skip_field(&pos);

  // Ground speed (optional, in m/s * 10 in newer protocols, or knots in older)
  data.ground_speed_valid = parse_int(&pos, &val);
  if (data.ground_speed_valid) {
    data.ground_speed = static_cast<uint16_t>(val);
  } else {
    skip_field(&pos);
  }

  // Climb rate (optional, in m/s * 10)
  data.climb_rate_valid = parse_int(&pos, &val);
  if (data.climb_rate_valid) {
    data.climb_rate = static_cast<int16_t>(val);
  } else {
    skip_field(&pos);
  }

  // Aircraft type (hex)
  if (parse_hex(&pos, &hex_val)) {
    data.aircraft_type = static_cast<FlarmAircraftType>(hex_val);
  } else {
    data.aircraft_type = FLARM_AIRCRAFT_UNKNOWN;
  }

  // NoTrack flag (optional, in newer protocol versions)
  if (parse_int(&pos, &val)) {
    data.no_track = (val != 0);
  }

  if (this->pflaa_callback_) {
    this->pflaa_callback_(data);
  }

  return true;
}

// Parse GPRMC sentence:
// $GPRMC,<time>,<status>,<lat>,<N/S>,<lon>,<E/W>,<speed>,<track>,<date>,<mag_var>,<E/W>,<mode>*<checksum> Example:
// $GPRMC,033633.00,A,2808.89958,S,15156.72253,E,0.019,,190714,,,A*69
bool FlarmParser::parse_gprmc_(const char *sentence) {
  const char *pos = sentence;
  float fval;

  // Time (HHMMSS.ss) - skip for now
  skip_field(&pos);

  // Status: A=valid, V=invalid
  char status = *pos;
  skip_field(&pos);

  if (status != 'A') {
    // Invalid fix
    this->ownship_.position_valid = false;
    return false;
  }

  // Latitude
  float lat;
  if (!parse_latitude(&pos, &lat)) {
    return false;
  }

  // Longitude
  float lon;
  if (!parse_longitude(&pos, &lon)) {
    return false;
  }

  // Ground speed in knots
  if (parse_float(&pos, &fval)) {
    this->ownship_.ground_speed = fval * 0.514444f;  // Convert knots to m/s
    this->ownship_.speed_valid = true;
  } else {
    this->ownship_.speed_valid = false;
    skip_field(&pos);
  }

  // Track (course over ground) in degrees
  if (parse_float(&pos, &fval)) {
    this->ownship_.track = fval;
    this->ownship_.track_valid = true;
  } else {
    this->ownship_.track_valid = false;
    skip_field(&pos);
  }

  // Update position
  this->ownship_.latitude = lat;
  this->ownship_.longitude = lon;
  this->ownship_.position_valid = true;
  this->ownship_.timestamp_ms = millis();

  ESP_LOGD(TAG, "GPRMC: lat=%.6f, lon=%.6f, speed=%.1f m/s, track=%.1f", lat, lon, this->ownship_.ground_speed,
           this->ownship_.track);

  if (this->ownship_callback_) {
    this->ownship_callback_(this->ownship_);
  }

  return true;
}

// Parse GPGGA sentence:
// $GPGGA,<time>,<lat>,<N/S>,<lon>,<E/W>,<fix>,<sats>,<hdop>,<alt>,<M>,<geoid>,<M>,<dgps_age>,<dgps_id>*<checksum>
// Example: $GPGGA,033633.00,2808.89958,S,15156.72253,E,1,08,1.69,462.1,M,35.3,M,,*45
bool FlarmParser::parse_gpgga_(const char *sentence) {
  const char *pos = sentence;
  int32_t ival;
  float fval;

  // Time - skip
  skip_field(&pos);

  // Latitude
  float lat;
  if (!parse_latitude(&pos, &lat)) {
    return false;
  }

  // Longitude
  float lon;
  if (!parse_longitude(&pos, &lon)) {
    return false;
  }

  // Fix quality: 0=invalid, 1=GPS, 2=DGPS, etc.
  if (!parse_int(&pos, &ival)) {
    return false;
  }
  this->ownship_.fix_quality = static_cast<uint8_t>(ival);

  if (ival == 0) {
    // No fix
    this->ownship_.position_valid = false;
    return false;
  }

  // Number of satellites
  if (parse_int(&pos, &ival)) {
    this->ownship_.satellites = static_cast<uint8_t>(ival);
  } else {
    skip_field(&pos);
  }

  // HDOP
  if (parse_float(&pos, &fval)) {
    this->ownship_.hdop = fval;
  } else {
    skip_field(&pos);
  }

  // Altitude above MSL in meters
  if (parse_float(&pos, &fval)) {
    this->ownship_.altitude_msl = fval;
    this->ownship_.altitude_msl_valid = true;
  } else {
    this->ownship_.altitude_msl_valid = false;
    skip_field(&pos);
  }

  // Update position
  this->ownship_.latitude = lat;
  this->ownship_.longitude = lon;
  this->ownship_.position_valid = true;
  this->ownship_.timestamp_ms = millis();

  ESP_LOGD(TAG, "GPGGA: lat=%.6f, lon=%.6f, alt=%.1f m, sats=%d, hdop=%.2f", lat, lon, this->ownship_.altitude_msl,
           this->ownship_.satellites, this->ownship_.hdop);

  if (this->ownship_callback_) {
    this->ownship_callback_(this->ownship_);
  }

  return true;
}

// Parse PGRMZ sentence: $PGRMZ,<altitude>,<units>,<fix_type>*<checksum>
// Example: $PGRMZ,1400,F,2*0F
// Altitude in feet, fix_type: 2=user altitude, 3=GPS altitude
bool FlarmParser::parse_pgrmz_(const char *sentence) {
  const char *pos = sentence;
  float fval;

  // Altitude value
  if (!parse_float(&pos, &fval)) {
    return false;
  }

  // Units: F=feet, M=meters
  char units = *pos;
  skip_field(&pos);

  // Convert to meters
  float altitude_m;
  if (units == 'F' || units == 'f') {
    altitude_m = fval * 0.3048f;  // Feet to meters
  } else if (units == 'M' || units == 'm') {
    altitude_m = fval;
  } else {
    return false;
  }

  this->ownship_.altitude_baro = altitude_m;
  this->ownship_.altitude_baro_valid = true;
  this->ownship_.timestamp_ms = millis();

  ESP_LOGD(TAG, "PGRMZ: baro_alt=%.1f m (%.0f ft)", altitude_m, fval);

  if (this->ownship_callback_) {
    this->ownship_callback_(this->ownship_);
  }

  return true;
}

}  // namespace skyecho
}  // namespace esphome
