#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace esphome::skyecho {

// FLARM alarm levels
enum FlarmAlarmLevel : uint8_t {
  FLARM_ALARM_NONE = 0,
  FLARM_ALARM_LOW = 1,
  FLARM_ALARM_IMPORTANT = 2,
  FLARM_ALARM_URGENT = 3,
};

// FLARM alarm types
enum FlarmAlarmType : uint8_t {
  FLARM_ALARM_TYPE_NONE = 0,
  FLARM_ALARM_TYPE_AIRCRAFT = 2,
  FLARM_ALARM_TYPE_OBSTACLE = 3,
  FLARM_ALARM_TYPE_TRAFFIC_INFO = 4,
};

// FLARM GPS status
enum FlarmGpsStatus : uint8_t {
  FLARM_GPS_NO_FIX = 0,
  FLARM_GPS_GROUND_2D = 1,
  FLARM_GPS_GROUND_3D = 2,
};

// FLARM power status
enum FlarmPowerStatus : uint8_t {
  FLARM_POWER_OFF = 0,
  FLARM_POWER_ON = 1,
};

// FLARM aircraft ID types
enum FlarmIdType : uint8_t {
  FLARM_ID_RANDOM = 0,
  FLARM_ID_ICAO = 1,
  FLARM_ID_FLARM = 2,
  FLARM_ID_OGN = 3,
};

// FLARM aircraft types (matching GDL90 emitter categories where possible)
enum FlarmAircraftType : uint8_t {
  FLARM_AIRCRAFT_UNKNOWN = 0,
  FLARM_AIRCRAFT_GLIDER = 1,
  FLARM_AIRCRAFT_TOW_PLANE = 2,
  FLARM_AIRCRAFT_HELICOPTER = 3,
  FLARM_AIRCRAFT_PARACHUTE = 4,
  FLARM_AIRCRAFT_DROP_PLANE = 5,
  FLARM_AIRCRAFT_HANG_GLIDER = 6,
  FLARM_AIRCRAFT_PARAGLIDER = 7,
  FLARM_AIRCRAFT_POWERED = 8,
  FLARM_AIRCRAFT_JET = 9,
  FLARM_AIRCRAFT_UFO = 10,
  FLARM_AIRCRAFT_BALLOON = 11,
  FLARM_AIRCRAFT_AIRSHIP = 12,
  FLARM_AIRCRAFT_UAV = 13,
  FLARM_AIRCRAFT_STATIC_OBJECT = 15,
};

// Ownship position data (from GPS sentences)
struct OwnshipPosition {
  float latitude;       // Degrees, positive = North
  float longitude;      // Degrees, positive = East
  float altitude_msl;   // Altitude above mean sea level in meters
  float altitude_baro;  // Barometric altitude in meters (from PGRMZ)
  float ground_speed;   // Ground speed in m/s
  float track;          // True track in degrees
  uint8_t satellites;   // Number of satellites in use
  float hdop;           // Horizontal dilution of precision
  uint8_t fix_quality;  // 0=invalid, 1=GPS, 2=DGPS
  bool position_valid;
  bool altitude_msl_valid;
  bool altitude_baro_valid;
  bool speed_valid;
  bool track_valid;
  uint32_t timestamp_ms;  // Last update timestamp
};

// PFLAU sentence data - heartbeat/status
struct FlarmPflau {
  uint8_t rx;              // Number of devices received
  uint8_t tx;              // Transmission status (0=no tx, 1=tx ok)
  FlarmGpsStatus gps;      // GPS status
  FlarmPowerStatus power;  // Power status
  FlarmAlarmLevel alarm_level;
  int16_t relative_bearing;  // -180 to 180 degrees, or empty
  FlarmAlarmType alarm_type;
  int32_t relative_vertical;   // Relative vertical distance in meters
  uint32_t relative_distance;  // Horizontal distance in meters
  uint32_t id;                 // 6-digit hex ID of most dangerous target
  bool bearing_valid;
  bool vertical_valid;
  bool distance_valid;
  bool id_valid;
};

// PFLAA sentence data - traffic report
struct FlarmPflaa {
  FlarmAlarmLevel alarm_level;
  int32_t relative_north;     // Relative north position in meters
  int32_t relative_east;      // Relative east position in meters
  int32_t relative_vertical;  // Relative vertical distance in meters
  FlarmIdType id_type;
  uint32_t id;            // 6-digit hex aircraft ID
  int16_t track;          // Ground track 0-359 degrees
  int16_t turn_rate;      // Turn rate (currently unused)
  uint16_t ground_speed;  // Ground speed in m/s * 10
  int16_t climb_rate;     // Climb rate in m/s * 10
  FlarmAircraftType aircraft_type;
  bool east_valid;
  bool track_valid;
  bool ground_speed_valid;
  bool climb_rate_valid;
  bool no_track;  // True if target requests no tracking
};

// Callback types
using FlarmPflauCallback = std::function<void(const FlarmPflau &)>;
using FlarmPflaaCallback = std::function<void(const FlarmPflaa &)>;
using OwnshipCallback = std::function<void(const OwnshipPosition &)>;

/**
 * FLARM NMEA sentence parser class
 * Parses FLARM proprietary sentences (PFLAU, PFLAA) and standard GPS sentences
 * (GPRMC, GPGGA, PGRMZ) to track ownship position.
 */
class FlarmParser {
 public:
  void set_pflau_callback(FlarmPflauCallback callback) { this->pflau_callback_ = std::move(callback); }
  void set_pflaa_callback(FlarmPflaaCallback callback) { this->pflaa_callback_ = std::move(callback); }
  void set_ownship_callback(OwnshipCallback callback) { this->ownship_callback_ = std::move(callback); }

  /**
   * Process incoming bytes from UART
   * @param data Pointer to incoming data
   * @param len Length of data
   */
  void process_bytes(const uint8_t *data, size_t len);

  /**
   * Get current ownship position
   */
  const OwnshipPosition &get_ownship() const { return this->ownship_; }

 protected:
  void process_line_();
  static bool verify_checksum(const char *sentence, size_t len);

  // NMEA field parsing helpers. Each advances *pos past the parsed field.
  static bool parse_int(const char **pos, int32_t *out);
  static bool parse_float(const char **pos, float *out);
  static bool parse_latitude(const char **pos, float *out);
  static bool parse_longitude(const char **pos, float *out);
  static bool skip_field(const char **pos);
  static const char *get_field(const char **pos, char *buf, size_t buf_size);

  bool parse_pflau_(const char *sentence) const;
  bool parse_pflaa_(const char *sentence) const;
  bool parse_gprmc_(const char *sentence);  // Recommended Minimum GPS data
  bool parse_gpgga_(const char *sentence);  // GPS Fix Data
  bool parse_pgrmz_(const char *sentence);  // Garmin altitude

  char line_buffer_[128]{};
  size_t line_pos_{0};

  OwnshipPosition ownship_{};

  FlarmPflauCallback pflau_callback_;
  FlarmPflaaCallback pflaa_callback_;
  OwnshipCallback ownship_callback_;
};

}  // namespace esphome::skyecho
