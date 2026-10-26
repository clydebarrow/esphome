#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::skyecho {

enum class DistanceUnit : uint8_t {
  DISTANCE_UNIT_KILOMETERS,
  DISTANCE_UNIT_NAUTICAL_MILES,
  DISTANCE_UNIT_STATUTE_MILES,
};

enum class AltitudeUnit : uint8_t {
  ALTITUDE_UNIT_FEET,
  ALTITUDE_UNIT_METERS,
};

enum class SpeedUnit : uint8_t {
  SPEED_UNIT_KNOTS,
  SPEED_UNIT_KILOMETERS_PER_HOUR,
  SPEED_UNIT_MILES_PER_HOUR,
};

/**
 * The units used to show distances, altitudes and speeds. The units are fixed for now;
 * the getters are the place to make them user-configurable later.
 */
class Units {
 public:
  DistanceUnit get_distance_unit() const { return DistanceUnit::DISTANCE_UNIT_NAUTICAL_MILES; }
  AltitudeUnit get_altitude_unit() const { return AltitudeUnit::ALTITUDE_UNIT_FEET; }
  SpeedUnit get_speed_unit() const { return SpeedUnit::SPEED_UNIT_KNOTS; }

  /// The length of one distance unit, in meters.
  float get_distance_unit_meters() const;
  /// The symbol shown after a distance, e.g. "NM".
  const char *get_distance_symbol() const;

  /// Write a distance given in meters, converted to the distance unit, e.g. "3.2 NM".
  void format_distance(char *buf, size_t len, float meters) const;
  /// Write a range ring distance given in meters, without a needless ".0", e.g. "2 NM" or "0.5 NM".
  void format_range(char *buf, size_t len, float meters) const;

  /// Write an altitude given in meters, converted to the altitude unit, e.g. "4520 ft".
  void format_altitude(char *buf, size_t len, float meters) const;
  /// Write a signed altitude difference given in meters, converted to the altitude unit, e.g. "+1200 ft".
  void format_altitude_difference(char *buf, size_t len, float meters) const;

  /// Write a speed given in meters per second, converted to the speed unit, e.g. "105 kt".
  void format_speed(char *buf, size_t len, float meters_per_second) const;
};

}  // namespace esphome::skyecho
