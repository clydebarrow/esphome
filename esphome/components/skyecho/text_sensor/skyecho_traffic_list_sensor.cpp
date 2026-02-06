#include "skyecho_traffic_list_sensor.h"
#include "esphome/core/log.h"
#include <cstdio>
#include <algorithm>

namespace esphome {
namespace skyecho {

static const char *const TAG_TRAFFIC = "skyecho.traffic_list";

void SkyEchoTrafficListSensor::dump_config() { ESP_LOGCONFIG(TAG_TRAFFIC, "SkyEcho Traffic List Sensor (merged)"); }

const char *SkyEchoTrafficListSensor::get_category_name(AircraftCategory category) {
  switch (category) {
    case CATEGORY_UNKNOWN:
      return "Unknown";
    case CATEGORY_LIGHT:
      return "Light";
    case CATEGORY_SMALL:
      return "Small";
    case CATEGORY_LARGE:
      return "Large";
    case CATEGORY_HIGH_VORTEX:
      return "HighVortex";
    case CATEGORY_HEAVY:
      return "Heavy";
    case CATEGORY_FIGHTER:
      return "Fighter";
    case CATEGORY_ROTORCRAFT:
      return "Heli";
    case CATEGORY_GLIDER:
      return "Glider";
    case CATEGORY_BALLOON:
      return "Balloon";
    case CATEGORY_PARACHUTE:
      return "Parachute";
    case CATEGORY_HANG_GLIDER:
      return "HangGlider";
    case CATEGORY_PARAGLIDER:
      return "Paraglider";
    case CATEGORY_UAV:
      return "UAV";
    case CATEGORY_SPACESHIP:
      return "Spaceship";
    case CATEGORY_SURFACE_EMERGENCY:
      return "Emergency";
    case CATEGORY_SURFACE_SERVICE:
      return "Service";
    case CATEGORY_TOW_PLANE:
      return "TowPlane";
    case CATEGORY_DROP_PLANE:
      return "DropPlane";
    default:
      return "Unknown";
  }
}

const char *SkyEchoTrafficListSensor::get_source_name(TrafficSource source) {
  switch (source) {
    case SOURCE_FLARM:
      return "F";
    case SOURCE_GDL90:
      return "G";
    case SOURCE_MERGED:
      return "M";
    default:
      return "?";
  }
}

void SkyEchoTrafficListSensor::update() {
  if (this->parent_ == nullptr) {
    return;
  }

  // Get merged traffic from TrafficManager
  const TrafficManager &tm = this->parent_->get_traffic_manager();
  const std::vector<Traffic> &traffic_list = tm.get_traffic();

  // Create a sorted copy by distance
  std::vector<const Traffic *> sorted_traffic;
  sorted_traffic.reserve(traffic_list.size());
  for (const auto &t : traffic_list) {
    sorted_traffic.push_back(&t);
  }
  std::sort(sorted_traffic.begin(), sorted_traffic.end(),
            [](const Traffic *a, const Traffic *b) { return a->distance_m < b->distance_m; });

  std::string output;
  size_t count = sorted_traffic.size();

  if (count == 0) {
    output = "No traffic detected";
  } else {
    // Add header with count
    char header[64];
    snprintf(header, sizeof(header), "Traffic: %zu\n", count);
    output = header;

    // Build the traffic list (limit to reasonable display size)
    size_t display_count = std::min(count, static_cast<size_t>(15));
    for (size_t i = 0; i < display_count; i++) {
      const Traffic *t = sorted_traffic[i];

      // Format: [Source] Type | Callsign | Address | Distance | RelAlt | Speed
      char line[128];

      // Convert speeds to more readable units
      float speed_kmh = t->speed_valid ? t->ground_speed * 3.6f : 0;  // m/s to km/h
      float distance_km = t->distance_m / 1000.0f;                    // m to km

      // Show relative vertical if available, otherwise absolute altitude
      float display_alt = t->relative_valid ? static_cast<float>(t->relative_vertical) : t->altitude;
      const char *alt_prefix = t->relative_valid ? "" : "@";

      // Format callsign or use hex ID
      const char *callsign = t->callsign[0] != '\0' ? t->callsign : "------";

      snprintf(line, sizeof(line), "[%s] %s | %s | %06X | %.1fkm | %s%+.0fm | %.0fkm/h\n", get_source_name(t->source),
               get_category_name(t->category), callsign, t->id, distance_km, alt_prefix, display_alt, speed_kmh);

      output += line;
    }

    // Show if more traffic exists
    if (count > display_count) {
      char more[32];
      snprintf(more, sizeof(more), "... +%zu more\n", count - display_count);
      output += more;
    }
  }

  // Publish the traffic list
  this->publish_state(output);
}

}  // namespace skyecho
}  // namespace esphome
