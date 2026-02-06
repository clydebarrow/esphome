#include "traffic_manager.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace esphome {
namespace skyecho {

static const char *const TAG = "traffic_mgr";

// Earth radius in meters
static constexpr float EARTH_RADIUS_M = 6371000.0f;

AircraftCategory TrafficManager::flarm_type_to_category(FlarmAircraftType type) {
  switch (type) {
    case FLARM_AIRCRAFT_GLIDER:
      return CATEGORY_GLIDER;
    case FLARM_AIRCRAFT_TOW_PLANE:
      return CATEGORY_TOW_PLANE;
    case FLARM_AIRCRAFT_HELICOPTER:
      return CATEGORY_ROTORCRAFT;
    case FLARM_AIRCRAFT_PARACHUTE:
      return CATEGORY_PARACHUTE;
    case FLARM_AIRCRAFT_DROP_PLANE:
      return CATEGORY_DROP_PLANE;
    case FLARM_AIRCRAFT_HANG_GLIDER:
      return CATEGORY_HANG_GLIDER;
    case FLARM_AIRCRAFT_PARAGLIDER:
      return CATEGORY_PARAGLIDER;
    case FLARM_AIRCRAFT_POWERED:
      return CATEGORY_LIGHT;
    case FLARM_AIRCRAFT_JET:
      return CATEGORY_LARGE;
    case FLARM_AIRCRAFT_BALLOON:
      return CATEGORY_BALLOON;
    case FLARM_AIRCRAFT_AIRSHIP:
      return CATEGORY_BALLOON;
    case FLARM_AIRCRAFT_UAV:
      return CATEGORY_UAV;
    default:
      return CATEGORY_UNKNOWN;
  }
}

AircraftCategory TrafficManager::gdl90_type_to_category(emitterCategory_t type) {
  switch (type) {
    case Light:
      return CATEGORY_LIGHT;
    case Small:
      return CATEGORY_SMALL;
    case Large:
      return CATEGORY_LARGE;
    case HighVortex:
      return CATEGORY_HIGH_VORTEX;
    case Heavy:
      return CATEGORY_HEAVY;
    case Fighter:
      return CATEGORY_FIGHTER;
    case Rotorcraft:
      return CATEGORY_ROTORCRAFT;
    case Glider:
      return CATEGORY_GLIDER;
    case Balloon:
      return CATEGORY_BALLOON;
    case Parachute:
      return CATEGORY_PARACHUTE;
    case HangGlider:
      return CATEGORY_HANG_GLIDER;
    case UAV:
      return CATEGORY_UAV;
    case Spaceship:
      return CATEGORY_SPACESHIP;
    case SurfaceEmergency:
      return CATEGORY_SURFACE_EMERGENCY;
    case SurfaceService:
      return CATEGORY_SURFACE_SERVICE;
    default:
      return CATEGORY_UNKNOWN;
  }
}

float TrafficManager::calculate_distance(float lat1, float lon1, float lat2, float lon2) {
  // Haversine formula for great-circle distance
  float lat1_rad = lat1 * M_PI / 180.0f;
  float lat2_rad = lat2 * M_PI / 180.0f;
  float dlat = (lat2 - lat1) * M_PI / 180.0f;
  float dlon = (lon2 - lon1) * M_PI / 180.0f;

  float a = sinf(dlat / 2) * sinf(dlat / 2) + cosf(lat1_rad) * cosf(lat2_rad) * sinf(dlon / 2) * sinf(dlon / 2);
  float c = 2 * atan2f(sqrtf(a), sqrtf(1 - a));

  return EARTH_RADIUS_M * c;
}

float TrafficManager::calculate_bearing(float lat1, float lon1, float lat2, float lon2) {
  float lat1_rad = lat1 * M_PI / 180.0f;
  float lat2_rad = lat2 * M_PI / 180.0f;
  float dlon_rad = (lon2 - lon1) * M_PI / 180.0f;

  float y = sinf(dlon_rad) * cosf(lat2_rad);
  float x = cosf(lat1_rad) * sinf(lat2_rad) - sinf(lat1_rad) * cosf(lat2_rad) * cosf(dlon_rad);

  float bearing = atan2f(y, x) * 180.0f / M_PI;
  if (bearing < 0) {
    bearing += 360.0f;
  }
  return bearing;
}

bool TrafficManager::tracks_match(float track1, float track2, float threshold) {
  float diff = fabsf(track1 - track2);
  // Handle wrap-around at 360 degrees
  if (diff > 180.0f) {
    diff = 360.0f - diff;
  }
  return diff <= threshold;
}

bool TrafficManager::relative_to_absolute(int32_t rel_north, int32_t rel_east, int32_t rel_vert, float &lat, float &lon,
                                          float &alt) {
  if (!this->ownship_.position_valid) {
    return false;
  }

  // Convert relative position to absolute
  // 1 degree latitude = 111,111 meters (approximately)
  // 1 degree longitude = 111,111 * cos(latitude) meters
  float lat_offset = rel_north / 111111.0f;
  float lon_offset = rel_east / (111111.0f * cosf(this->ownship_.latitude * M_PI / 180.0f));

  lat = this->ownship_.latitude + lat_offset;
  lon = this->ownship_.longitude + lon_offset;
  alt = (this->ownship_.altitude_msl_valid ? this->ownship_.altitude_msl : 0) + rel_vert;

  return true;
}

void TrafficManager::update_relative_info(Traffic &traffic) {
  if (!this->ownship_.position_valid || !traffic.position_valid) {
    return;
  }

  traffic.distance_m =
      calculate_distance(this->ownship_.latitude, this->ownship_.longitude, traffic.latitude, traffic.longitude);
  traffic.bearing = static_cast<int16_t>(
      calculate_bearing(this->ownship_.latitude, this->ownship_.longitude, traffic.latitude, traffic.longitude));

  // Update relative position
  float lat_diff = traffic.latitude - this->ownship_.latitude;
  float lon_diff = traffic.longitude - this->ownship_.longitude;
  traffic.relative_north = static_cast<int32_t>(lat_diff * 111111.0f);
  traffic.relative_east = static_cast<int32_t>(lon_diff * 111111.0f * cosf(this->ownship_.latitude * M_PI / 180.0f));
  traffic.relative_vertical =
      static_cast<int32_t>(traffic.altitude - (this->ownship_.altitude_msl_valid ? this->ownship_.altitude_msl : 0));
  traffic.relative_valid = true;
}

Traffic *TrafficManager::find_by_id(uint32_t id) {
  for (auto &t : this->traffic_) {
    if (t.id == id) {
      return &t;
    }
  }
  return nullptr;
}

Traffic *TrafficManager::find_by_position(float lat, float lon, float alt, float track, float speed) {
  Traffic *best_match = nullptr;
  float best_score = 0;

  for (auto &t : this->traffic_) {
    if (!t.position_valid) {
      continue;
    }

    // Calculate position difference
    float dist = calculate_distance(lat, lon, t.latitude, t.longitude);
    if (dist > POSITION_MATCH_THRESHOLD_M) {
      continue;
    }

    // Calculate altitude difference (if valid)
    float alt_diff = 0;
    if (t.altitude_valid) {
      alt_diff = fabsf(alt - t.altitude);
      if (alt_diff > 300) {  // More than 300m altitude difference
        continue;
      }
    }

    // Check track similarity (if valid)
    bool track_ok = true;
    if (t.track_valid && track >= 0) {
      track_ok = tracks_match(track, t.track, TRACK_MATCH_THRESHOLD_DEG);
    }

    // Check speed similarity (if valid)
    bool speed_ok = true;
    if (t.speed_valid && speed >= 0) {
      speed_ok = fabsf(speed - t.ground_speed) <= SPEED_MATCH_THRESHOLD_MS;
    }

    if (!track_ok || !speed_ok) {
      continue;
    }

    // Calculate match score (lower is better)
    float score = dist + alt_diff;
    if (best_match == nullptr || score < best_score) {
      best_match = &t;
      best_score = score;
    }
  }

  return best_match;
}

void TrafficManager::update_from_flarm(const FlarmPflaa &flarm) {
  uint32_t now = millis();
  bool is_new = false;

  // First try to find by ID
  Traffic *traffic = find_by_id(flarm.id);

  // If not found by ID, try to find by position (for GDL90 traffic that might match)
  if (traffic == nullptr && flarm.east_valid) {
    float lat, lon, alt;
    if (relative_to_absolute(flarm.relative_north, flarm.relative_east, flarm.relative_vertical, lat, lon, alt)) {
      float track = flarm.track_valid ? static_cast<float>(flarm.track) : -1;
      float speed = flarm.ground_speed_valid ? static_cast<float>(flarm.ground_speed) / 10.0f : -1;
      traffic = find_by_position(lat, lon, alt, track, speed);

      if (traffic != nullptr) {
        ESP_LOGD(TAG, "FLARM ID %06X matched existing traffic ID %06X by position", flarm.id, traffic->id);
        // Keep the existing ID (likely ICAO) but note this is now merged
        traffic->source = SOURCE_MERGED;
      }
    }
  }

  // Create new entry if not found
  if (traffic == nullptr) {
    if (this->traffic_.size() >= MAX_TRAFFIC) {
      ESP_LOGW(TAG, "Traffic list full, ignoring new FLARM target %06X", flarm.id);
      return;
    }

    this->traffic_.emplace_back();
    traffic = &this->traffic_.back();
    memset(traffic, 0, sizeof(Traffic));
    traffic->id = flarm.id;
    traffic->id_type = static_cast<uint8_t>(flarm.id_type);
    traffic->source = SOURCE_FLARM;
    traffic->first_seen_ms = now;
    is_new = true;
    ESP_LOGD(TAG, "New FLARM traffic: ID=%06X", flarm.id);
  }

  // Update traffic data
  traffic->last_seen_ms = now;
  traffic->last_flarm_ms = now;
  traffic->last_update_source = SOURCE_FLARM;
  traffic->alarm_level = flarm.alarm_level;
  traffic->no_track = flarm.no_track;
  traffic->category = flarm_type_to_category(flarm.aircraft_type);

  // Store relative position
  traffic->relative_north = flarm.relative_north;
  traffic->relative_east = flarm.east_valid ? flarm.relative_east : 0;
  traffic->relative_vertical = flarm.relative_vertical;
  traffic->relative_valid = flarm.east_valid;

  // Convert to absolute position if possible
  if (flarm.east_valid) {
    float lat, lon, alt;
    if (relative_to_absolute(flarm.relative_north, flarm.relative_east, flarm.relative_vertical, lat, lon, alt)) {
      traffic->latitude = lat;
      traffic->longitude = lon;
      traffic->altitude = alt;
      traffic->position_valid = true;
      traffic->altitude_valid = true;
    }
  }

  // Update dynamics
  if (flarm.track_valid) {
    traffic->track = static_cast<float>(flarm.track);
    traffic->track_valid = true;
  }
  if (flarm.ground_speed_valid) {
    traffic->ground_speed = static_cast<float>(flarm.ground_speed) / 10.0f;  // Convert from dm/s to m/s
    traffic->speed_valid = true;
  }
  if (flarm.climb_rate_valid) {
    traffic->climb_rate = static_cast<float>(flarm.climb_rate) / 10.0f;  // Convert from dm/s to m/s
    traffic->climb_valid = true;
  }

  // Update distance/bearing
  update_relative_info(*traffic);

  // Invoke callback
  if (this->callback_) {
    this->callback_(*traffic, is_new);
  }
}

void TrafficManager::update_from_gdl90(const gdl90PositionReport_t &gdl90) {
  uint32_t now = millis();
  bool is_new = false;

  // First try to find by ID
  Traffic *traffic = find_by_id(gdl90.address);

  // If not found by ID, try to find by position (for FLARM traffic that might match)
  if (traffic == nullptr) {
    traffic = find_by_position(gdl90.latitude, gdl90.longitude, gdl90.altitude, gdl90.track, gdl90.groundSpeed);

    if (traffic != nullptr) {
      ESP_LOGD(TAG, "GDL90 ID %06X matched existing traffic ID %06X by position", gdl90.address, traffic->id);
      // Update to use ICAO ID if this was a FLARM-only target
      if (traffic->source == SOURCE_FLARM && gdl90.addressType == EmitterAdsbIcao) {
        ESP_LOGD(TAG, "Upgrading FLARM ID %06X to ICAO ID %06X", traffic->id, gdl90.address);
        traffic->id = gdl90.address;
        traffic->id_type = static_cast<uint8_t>(gdl90.addressType);
      }
      traffic->source = SOURCE_MERGED;
    }
  }

  // Create new entry if not found
  if (traffic == nullptr) {
    if (this->traffic_.size() >= MAX_TRAFFIC) {
      ESP_LOGW(TAG, "Traffic list full, ignoring new GDL90 target %06X", gdl90.address);
      return;
    }

    this->traffic_.emplace_back();
    traffic = &this->traffic_.back();
    memset(traffic, 0, sizeof(Traffic));
    traffic->id = gdl90.address;
    traffic->id_type = static_cast<uint8_t>(gdl90.addressType);
    traffic->source = SOURCE_GDL90;
    traffic->first_seen_ms = now;
    is_new = true;
    ESP_LOGD(TAG, "New GDL90 traffic: ID=%06X", gdl90.address);
  }

  // Update traffic data
  traffic->last_seen_ms = now;
  traffic->last_gdl90_ms = now;
  traffic->last_update_source = SOURCE_GDL90;
  traffic->category = gdl90_type_to_category(gdl90.category);

  // Update alarm level based on GDL90 priority code
  if (gdl90.priorityCode > 0) {
    traffic->alarm_level = std::min(static_cast<uint8_t>(gdl90.priorityCode), static_cast<uint8_t>(3));
  }

  // Position
  traffic->latitude = gdl90.latitude;
  traffic->longitude = gdl90.longitude;
  traffic->altitude = gdl90.altitude;
  traffic->position_valid = true;
  traffic->altitude_valid = true;

  // Dynamics
  traffic->ground_speed = gdl90.groundSpeed;
  traffic->track = gdl90.track;
  traffic->climb_rate = gdl90.verticalSpeed;
  traffic->speed_valid = true;
  traffic->track_valid = gdl90.isHeading;  // isHeading indicates track is valid
  traffic->climb_valid = true;

  // Callsign
  memcpy(traffic->callsign, gdl90.callsign, sizeof(traffic->callsign) - 1);
  traffic->callsign[sizeof(traffic->callsign) - 1] = '\0';

  // Update distance/bearing
  update_relative_info(*traffic);

  // Invoke callback
  if (this->callback_) {
    this->callback_(*traffic, is_new);
  }
}

void TrafficManager::cleanup() {
  uint32_t now = millis();

  // Remove expired traffic
  auto it = std::remove_if(this->traffic_.begin(), this->traffic_.end(), [now](const Traffic &t) {
    bool expired = (now - t.last_seen_ms) > TRAFFIC_TIMEOUT_MS;
    if (expired) {
      ESP_LOGD(TAG, "Removing expired traffic ID=%06X", t.id);
    }
    return expired;
  });

  if (it != this->traffic_.end()) {
    this->traffic_.erase(it, this->traffic_.end());
  }
}

}  // namespace skyecho
}  // namespace esphome
