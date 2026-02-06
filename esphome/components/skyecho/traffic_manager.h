#pragma once

#include "flarm.h"
#include "gdl90.h"
#include <cstdint>
#include <vector>
#include <functional>

namespace esphome {
namespace skyecho {

// Traffic data source
enum TrafficSource : uint8_t {
  SOURCE_UNKNOWN = 0,
  SOURCE_FLARM = 1,   // From FLARM UART (PFLAA)
  SOURCE_GDL90 = 2,   // From SkyEcho/GDL90 UDP
  SOURCE_MERGED = 3,  // Data merged from multiple sources
};

// Unified aircraft type (superset of FLARM and GDL90 types)
enum AircraftCategory : uint8_t {
  CATEGORY_UNKNOWN = 0,
  CATEGORY_LIGHT = 1,
  CATEGORY_SMALL = 2,
  CATEGORY_LARGE = 3,
  CATEGORY_HIGH_VORTEX = 4,
  CATEGORY_HEAVY = 5,
  CATEGORY_FIGHTER = 6,
  CATEGORY_ROTORCRAFT = 7,
  CATEGORY_GLIDER = 8,
  CATEGORY_BALLOON = 9,
  CATEGORY_PARACHUTE = 10,
  CATEGORY_HANG_GLIDER = 11,
  CATEGORY_PARAGLIDER = 12,
  CATEGORY_UAV = 13,
  CATEGORY_SPACESHIP = 14,
  CATEGORY_SURFACE_EMERGENCY = 15,
  CATEGORY_SURFACE_SERVICE = 16,
  CATEGORY_TOW_PLANE = 17,
  CATEGORY_DROP_PLANE = 18,
};

// Unified traffic record
struct Traffic {
  uint32_t id;                       // Aircraft ID (ICAO or FLARM ID)
  uint8_t id_type;                   // ID type (ICAO, FLARM, random, etc.)
  TrafficSource source;              // Primary data source
  TrafficSource last_update_source;  // Source of most recent update

  // Position (absolute)
  float latitude;   // Degrees
  float longitude;  // Degrees
  float altitude;   // Meters MSL
  bool position_valid;
  bool altitude_valid;

  // Relative position (for display without ownship GPS)
  int32_t relative_north;     // Meters from ownship
  int32_t relative_east;      // Meters from ownship
  int32_t relative_vertical;  // Meters from ownship
  bool relative_valid;

  // Dynamics
  float ground_speed;  // m/s
  float track;         // Degrees true
  float climb_rate;    // m/s
  bool speed_valid;
  bool track_valid;
  bool climb_valid;

  // Classification
  AircraftCategory category;
  char callsign[9];     // Null-terminated, max 8 chars
  uint8_t alarm_level;  // 0=none, 1=low, 2=important, 3=urgent

  // Metadata
  uint32_t first_seen_ms;  // When first detected
  uint32_t last_seen_ms;   // Last update timestamp
  uint32_t last_flarm_ms;  // Last FLARM update
  uint32_t last_gdl90_ms;  // Last GDL90 update
  bool no_track;           // Aircraft requested no tracking

  // Distance calculation helpers
  float distance_m;  // Calculated distance to ownship
  int16_t bearing;   // Bearing from ownship (degrees)
};

// Callback for traffic updates
using TrafficCallback = std::function<void(const Traffic &, bool is_new)>;

/**
 * Traffic manager - merges traffic from FLARM and GDL90 sources
 */
class TrafficManager {
 public:
  // Configuration
  static constexpr uint32_t TRAFFIC_TIMEOUT_MS = 30000;     // Remove after 30s without update
  static constexpr uint32_t STALE_THRESHOLD_MS = 5000;      // Consider stale after 5s
  static constexpr float POSITION_MATCH_THRESHOLD_M = 500;  // Position match within 500m
  static constexpr float TRACK_MATCH_THRESHOLD_DEG = 30;    // Track match within 30 degrees
  static constexpr float SPEED_MATCH_THRESHOLD_MS = 10;     // Speed match within 10 m/s
  static constexpr size_t MAX_TRAFFIC = 50;                 // Maximum tracked aircraft

  TrafficManager() = default;

  /**
   * Set callback for traffic updates
   */
  void set_callback(TrafficCallback callback) { this->callback_ = std::move(callback); }

  /**
   * Update ownship position (needed for relative-to-absolute conversion)
   */
  void set_ownship(const OwnshipPosition &ownship) { this->ownship_ = ownship; }

  /**
   * Add or update traffic from FLARM PFLAA sentence
   */
  void update_from_flarm(const FlarmPflaa &flarm);

  /**
   * Add or update traffic from GDL90 traffic report
   */
  void update_from_gdl90(const gdl90PositionReport_t &gdl90);

  /**
   * Remove stale traffic entries
   * Call periodically (e.g., every second)
   */
  void cleanup();

  /**
   * Get all current traffic
   */
  const std::vector<Traffic> &get_traffic() const { return this->traffic_; }

  /**
   * Get traffic count
   */
  size_t get_count() const { return this->traffic_.size(); }

  /**
   * Find traffic by ID
   * @return pointer to traffic or nullptr if not found
   */
  Traffic *find_by_id(uint32_t id);

  /**
   * Find traffic by position similarity
   * @return pointer to traffic or nullptr if not found
   */
  Traffic *find_by_position(float lat, float lon, float alt, float track, float speed);

 protected:
  /**
   * Convert FLARM aircraft type to unified category
   */
  static AircraftCategory flarm_type_to_category(FlarmAircraftType type);

  /**
   * Convert GDL90 emitter category to unified category
   */
  static AircraftCategory gdl90_type_to_category(emitterCategory_t type);

  /**
   * Calculate distance between two positions
   */
  static float calculate_distance(float lat1, float lon1, float lat2, float lon2);

  /**
   * Calculate bearing from pos1 to pos2
   */
  static float calculate_bearing(float lat1, float lon1, float lat2, float lon2);

  /**
   * Check if two tracks are similar (accounting for wrap-around)
   */
  static bool tracks_match(float track1, float track2, float threshold);

  /**
   * Convert FLARM relative position to absolute using ownship
   */
  bool relative_to_absolute(int32_t rel_north, int32_t rel_east, int32_t rel_vert, float &lat, float &lon, float &alt);

  /**
   * Update distance and bearing for traffic relative to ownship
   */
  void update_relative_info(Traffic &traffic);

  std::vector<Traffic> traffic_;
  OwnshipPosition ownship_{};
  TrafficCallback callback_;
};

}  // namespace skyecho
}  // namespace esphome
