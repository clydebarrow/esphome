#include "units.h"

#include <cmath>
#include <cstdio>

namespace esphome::skyecho {

static constexpr float METERS_PER_KILOMETER = 1000.0f;
static constexpr float METERS_PER_NAUTICAL_MILE = 1852.0f;
static constexpr float METERS_PER_STATUTE_MILE = 1609.344f;
static constexpr float METERS_PER_FOOT = 0.3048f;
static constexpr float SECONDS_PER_HOUR = 3600.0f;

float Units::get_distance_unit_meters() const {
  switch (this->get_distance_unit()) {
    case DistanceUnit::DISTANCE_UNIT_KILOMETERS:
      return METERS_PER_KILOMETER;
    case DistanceUnit::DISTANCE_UNIT_STATUTE_MILES:
      return METERS_PER_STATUTE_MILE;
    case DistanceUnit::DISTANCE_UNIT_NAUTICAL_MILES:
    default:
      return METERS_PER_NAUTICAL_MILE;
  }
}

const char *Units::get_distance_symbol() const {
  switch (this->get_distance_unit()) {
    case DistanceUnit::DISTANCE_UNIT_KILOMETERS:
      return "km";
    case DistanceUnit::DISTANCE_UNIT_STATUTE_MILES:
      return "mi";
    case DistanceUnit::DISTANCE_UNIT_NAUTICAL_MILES:
    default:
      return "NM";
  }
}

void Units::format_distance(char *buf, size_t len, float meters) const {
  const float value = meters / this->get_distance_unit_meters();
  // One decimal place while close, where it matters most.
  snprintf(buf, len, value < 10.0f ? "%.1f %s" : "%.0f %s", value, this->get_distance_symbol());
}

void Units::format_range(char *buf, size_t len, float meters) const {
  const float value = meters / this->get_distance_unit_meters();
  snprintf(buf, len, value == roundf(value) ? "%.0f %s" : "%.1f %s", value, this->get_distance_symbol());
}

void Units::format_altitude(char *buf, size_t len, float meters) const {
  if (this->get_altitude_unit() == AltitudeUnit::ALTITUDE_UNIT_METERS) {
    snprintf(buf, len, "%.0f m", meters);
  } else {
    snprintf(buf, len, "%.0f ft", roundf(meters / METERS_PER_FOOT / 10.0f) * 10.0f);
  }
}

void Units::format_altitude_difference(char *buf, size_t len, float meters) const {
  if (this->get_altitude_unit() == AltitudeUnit::ALTITUDE_UNIT_METERS) {
    snprintf(buf, len, "%+.0f m", roundf(meters / 10.0f) * 10.0f);
  } else {
    snprintf(buf, len, "%+.0f ft", roundf(meters / METERS_PER_FOOT / 100.0f) * 100.0f);
  }
}

void Units::format_speed(char *buf, size_t len, float meters_per_second) const {
  const float meters_per_hour = meters_per_second * SECONDS_PER_HOUR;
  switch (this->get_speed_unit()) {
    case SpeedUnit::SPEED_UNIT_KILOMETERS_PER_HOUR:
      snprintf(buf, len, "%.0f km/h", meters_per_hour / METERS_PER_KILOMETER);
      break;
    case SpeedUnit::SPEED_UNIT_MILES_PER_HOUR:
      snprintf(buf, len, "%.0f mph", meters_per_hour / METERS_PER_STATUTE_MILE);
      break;
    case SpeedUnit::SPEED_UNIT_KNOTS:
    default:
      snprintf(buf, len, "%.0f kt", meters_per_hour / METERS_PER_NAUTICAL_MILE);
      break;
  }
}

}  // namespace esphome::skyecho
