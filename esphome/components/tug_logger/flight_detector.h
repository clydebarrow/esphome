#pragma once

#include <cstdint>
#include <ctime>

namespace esphome::tug_logger {

static constexpr float METERS_PER_FOOT = 0.3048f;
static constexpr float KNOTS_TO_MPS = 1852.0f / 3600.0f;

/// One position report. Times are on a monotonic clock; a value is ignored unless its flag is set.
struct PositionSample {
  uint32_t now_ms{0};
  /// UTC seconds, or 0 if the clock has not been set.
  time_t epoch{0};
  bool speed_valid{false};
  float ground_speed_mps{0.0f};
  bool geometric_valid{false};
  float geometric_m{0.0f};
  bool pressure_valid{false};
  float pressure_m{0.0f};
};

enum class AltitudeKind : uint8_t {
  ALTITUDE_KIND_NONE,
  ALTITUDE_KIND_GEOMETRIC,
  ALTITUDE_KIND_PRESSURE,
};

struct DetectorSettings {
  float takeoff_speed_mps{40.0f * KNOTS_TO_MPS};
  uint32_t takeoff_confirm_ms{5000};
  float landing_speed_mps{15.0f * KNOTS_TO_MPS};
  uint32_t landing_confirm_ms{30000};
  /// Used only when there was no altitude on the ground before takeoff.
  float field_elevation_ft{0.0f};
  /// A ground altitude older than this is not used as the reference.
  uint32_t max_reference_age_ms{300000};
  /// A silence longer than this in flight makes the figures approximate.
  uint32_t max_data_gap_ms{60000};
};

/// What a flight measured so far. Also the state written to the card so a restart does not lose a flight.
struct FlightSnapshot {
  /// UTC seconds of takeoff; 0 until the clock is known.
  time_t launch{0};
  uint32_t elapsed_s{0};
  uint32_t climb_s{0};
  int32_t max_height_ft{0};
  /// Height used the field elevation, or data was missing for a while.
  bool approximate{false};
  AltitudeKind kind{AltitudeKind::ALTITUDE_KIND_NONE};
  /// Altitude that counts as zero height, in the units of kind (meters).
  float reference_m{0.0f};
  bool has_reference{false};
};

enum class FlightEvent : uint8_t {
  FLIGHT_EVENT_NONE,
  FLIGHT_EVENT_TAKEOFF,
  FLIGHT_EVENT_LANDED,
};

/// Finds takeoffs and landings from ground speed, and measures the flight between them. Height is measured from the
/// altitude seen on the ground just before takeoff with the same kind of altitude, which cancels QNH and geoid errors.
class FlightDetector {
 public:
  FlightDetector() = default;
  explicit FlightDetector(const DetectorSettings &settings) : settings_(settings) {}

  FlightEvent update(const PositionSample &sample);
  void set_field_elevation_ft(float elevation_ft) { this->settings_.field_elevation_ft = elevation_ft; }

  bool is_flying() const { return this->flying_; }
  /// The flight in progress, or after FLIGHT_EVENT_LANDED the flight that has just ended.
  const FlightSnapshot &flight() const { return this->flight_; }
  /// When the launch happened on the monotonic clock, for working out its time when the clock is set later.
  uint32_t launch_ms() const { return this->launch_ms_; }
  /// Height above the reference at the last sample, in feet.
  int32_t current_height_ft() const { return this->current_height_ft_; }

  /// Carries on a flight that was in progress before a restart.
  void resume(const FlightSnapshot &snapshot, uint32_t now_ms);

 protected:
  void start_flight_(const PositionSample &sample);
  void track_flight_(const PositionSample &sample);
  bool altitude_for_(const PositionSample &sample, float &altitude_m) const;
  float median_height_(float height_ft);

  DetectorSettings settings_{};
  FlightSnapshot flight_{};
  bool flying_{false};

  // First sample of the run of samples above or below the threshold
  bool crossing_{false};
  uint32_t crossing_ms_{0};
  time_t crossing_epoch_{0};

  uint32_t launch_ms_{0};
  uint32_t last_sample_ms_{0};
  bool has_last_sample_{false};
  int32_t current_height_ft_{0};

  // Altitudes while slow on the ground
  bool ground_geometric_valid_{false};
  float ground_geometric_m_{0.0f};
  uint32_t ground_geometric_ms_{0};
  bool ground_pressure_valid_{false};
  float ground_pressure_m_{0.0f};
  uint32_t ground_pressure_ms_{0};

  // Last three heights; a median rejects a single bad fix
  float heights_[3]{};
  uint8_t height_count_{0};
};

}  // namespace esphome::tug_logger
