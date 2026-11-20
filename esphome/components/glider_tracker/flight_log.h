#pragma once

#include <cstddef>
#include <cstdint>

#include "glider_packets.h"

namespace esphome::glider_tracker {

/// One GPS reading, once a second.
struct Sample {
  uint32_t now;  // Unix time, 0 if the clock isn't known
  bool fix;
  double latitude;
  double longitude;
  int16_t altitude_m;
  bool speed_valid;
  float speed_kmh;
};

/// Flight detection from ground speed, as the CubeCell tracker does it (htcc-ab02s/include/config.h).
struct FlightSettings {
  // Takeoff needs this speed for a few seconds running
  float takeoff_kmh{40};
  uint8_t takeoff_seconds{5};
  // Landing needs the speed to stay low for a while, and only a speed well above the landing limit counts as moving
  // again, so a slow roll-out lands
  float landed_kmh{10};
  float moving_again_kmh{25};
  uint32_t landed_seconds{30};
};

static constexpr size_t QUEUE_SLOTS = 8;
static constexpr size_t SLOT = Summary::SIZE;

/// What is kept in flash: the flight in progress and the summaries waiting for the clubhouse. A slot is in use when
/// its first byte is SUMMARY.
struct FlightStore {
  static constexpr uint32_t MAGIC = 0x31514C47;  // "GLQ1", as the CubeCell's
  uint32_t magic;
  uint8_t flight[SLOT];
  uint8_t queue[QUEUE_SLOTS][SLOT];
};

/**
 * A glider's flights, from takeoff to the clubhouse's acknowledgement of its summary. The flight in progress has its
 * takeoff fields set, and its landing fields once the glider has slowed down; on landing its summary joins a queue of
 * 8, the oldest dropped when full. A summary, and the takeoff report of the flight in progress, are sent until
 * acknowledged; each is identified by its takeoff time, so sending one again is always safe.
 */
class FlightLog {
 public:
  enum class Event : uint8_t { NONE, TAKEOFF, LANDED };

  explicit FlightLog(const FlightSettings &settings = {}) : settings_(settings) { this->clear_(); }

  /// The store as saved before a restart. A flight in progress goes on, and its takeoff is reported again.
  void load(const FlightStore &store);
  const FlightStore &store() const { return this->store_; }
  /// True once after the store has changed, when it should be saved.
  bool take_changed() {
    const bool changed = this->changed_;
    this->changed_ = false;
    return changed;
  }

  /// Detects takeoff and landing; call once a second.
  Event update(const Sample &sample);

  bool is_flying() const { return this->store_.flight[0] == SUMMARY; }
  /// The flight in progress; false when on the ground.
  bool get_flight(Summary *flight) const { return flight->decode(this->store_.flight, SLOT); }
  /// The takeoff report to send, while the clubhouse hasn't acknowledged it.
  bool get_takeoff_report(Takeoff *report) const;
  /// The oldest summary waiting, as sent; nullptr when there is none.
  const uint8_t *next_summary() const;
  size_t queued() const;

  /// Takes the clubhouse's acknowledgement. True if it was for something waiting.
  bool acknowledge(const Ack &ack);

  /// A fake flight of an hour ending now, at the sample's position, for bench tests. Without a clock its time is
  /// made up, and still differs each time.
  void fake_flight(const Sample &sample, uint32_t uptime_s);
  /// A fake takeoff now, or the landing of the flight in progress. A fake flight lands only this way, not from GPS.
  Event fake_takeoff_or_landing(const Sample &sample, uint32_t uptime_s);

 protected:
  void clear_();
  void take_off_(const Summary &candidate);
  void land_(const Summary &flight);
  void enqueue_(const Summary &summary);

  FlightSettings settings_;
  FlightStore store_{};
  bool changed_{false};
  bool takeoff_pending_{false};
  bool fake_airborne_{false};
  uint8_t fast_seconds_{0};
  Summary candidate_{};
  uint32_t slow_since_{0};
};

}  // namespace esphome::glider_tracker
