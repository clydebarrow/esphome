#include "skyecho_traffic_list_sensor.h"
#include "esphome/core/log.h"
#include <cstdio>

namespace esphome {
namespace skyecho {

static const char *const TAG_TRAFFIC = "skyecho.traffic_list";

void SkyEchoTrafficListSensor::dump_config() { ESP_LOGCONFIG(TAG_TRAFFIC, "SkyEcho Traffic List Sensor"); }

const char *SkyEchoTrafficListSensor::get_category_name(emitterCategory_t category) {
  switch (category) {
    case None:
      return "None";
    case Light:
      return "Light";
    case Small:
      return "Small";
    case Large:
      return "Large";
    case HighVortex:
      return "HighVortex";
    case Heavy:
      return "Heavy";
    case Fighter:
      return "Fighter";
    case Rotorcraft:
      return "Heli";
    case Glider:
      return "Glider";
    case Balloon:
      return "Balloon";
    case Parachute:
      return "Parachute";
    case HangGlider:
      return "HangGlider";
    case UAV:
      return "UAV";
    case Spaceship:
      return "Spaceship";
    case SurfaceEmergency:
      return "Emergency";
    case SurfaceService:
      return "Service";
    case Obstacle:
      return "Obstacle";
    case ObstacleCluster:
      return "ObstCluster";
    case ObstacleLine:
      return "ObstLine";
    default:
      return "Unknown";
  }
}

void SkyEchoTrafficListSensor::update() {
  if (this->parent_ == nullptr) {
    return;
  }

  // Get all traffic, sorted by distance (nearest first)
  traffic_t traffic[MAX_TRAFFIC_TRACKED];
  this->parent_->getTraffic(traffic, MAX_TRAFFIC_TRACKED);

  std::string output;
  int active_count = 0;

  // Build the traffic list
  for (size_t i = 0; i < MAX_TRAFFIC_TRACKED; i++) {
    if (!traffic[i].active)
      continue;

    active_count++;

    // Format: Type | Callsign | Address | Distance | Altitude | Speed
    char line[128];

    // Get altitude relative to ownship if available
    ownship_t ownship;
    float rel_altitude = traffic[i].report.altitude;
    if (this->parent_->getOwnshipPosition(&ownship)) {
      rel_altitude = traffic[i].report.altitude - ownship.report.altitude;
    }

    // Convert speeds to more readable units
    float speed_kmh = traffic[i].report.groundSpeed * 3.6f;  // m/s to km/h
    float distance_km = traffic[i].distance / 1000.0f;       // m to km

    snprintf(line, sizeof(line), "%s | %s | %06X | %.1fkm | %+.0fm | %.0fkm/h\n",
             this->get_category_name(traffic[i].report.category), traffic[i].report.callsign,
             (unsigned int) traffic[i].report.address, distance_km, rel_altitude, speed_kmh);

    output += line;
  }

  // If no traffic, show message
  if (active_count == 0) {
    output = "No traffic detected";
  } else {
    // Add header
    std::string header = "Traffic: " + std::to_string(active_count) + "\n";
    output = header + output;
  }

  // Publish the traffic list
  this->publish_state(output);
}

}  // namespace skyecho
}  // namespace esphome
