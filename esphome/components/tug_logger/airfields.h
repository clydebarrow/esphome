#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace esphome::tug_logger {

/// An airfield a tug may launch from, from the Worker's list.
struct Airfield {
  std::string name;
  double lat{0.0};
  double lon{0.0};
  float elevation_ft{0.0f};
};

/// A tug this close to an airfield as it takes off is launching from it.
static constexpr double AIRFIELD_RADIUS_M = 3000.0;

/// The distance between two points, in metres; flat-earth, which is close enough over a few kilometres.
inline double airfield_distance_m(double lat1, double lon1, double lat2, double lon2) {
  constexpr double EARTH_RADIUS_M = 6371000.0;
  constexpr double RADIANS = M_PI / 180.0;
  const double x = (lon2 - lon1) * RADIANS * std::cos((lat1 + lat2) * 0.5 * RADIANS);
  const double y = (lat2 - lat1) * RADIANS;
  return std::sqrt(x * x + y * y) * EARTH_RADIUS_M;
}

/// The nearest airfield within AIRFIELD_RADIUS_M of the position, or nullptr.
inline const Airfield *nearest_airfield(const std::vector<Airfield> &airfields, double lat, double lon) {
  const Airfield *best = nullptr;
  double best_m = AIRFIELD_RADIUS_M;
  for (const Airfield &airfield : airfields) {
    const double distance = airfield_distance_m(lat, lon, airfield.lat, airfield.lon);
    if (distance <= best_m) {
      best_m = distance;
      best = &airfield;
    }
  }
  return best;
}

}  // namespace esphome::tug_logger
