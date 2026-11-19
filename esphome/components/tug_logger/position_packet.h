#pragma once
// The tug's position report for the live map: the same 24 bytes as a glider tracker's (glider::Position in
// glider-tests/glider_packets.h in the glider-lora worktree), broadcast unencrypted over LoRa through the relay. The
// receiver tells a tug from a glider by the sender's address, which the Worker gives each device. Keep the two in step.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace esphome::tug_logger {

inline void position_put16(uint8_t *p, uint16_t value) {
  p[0] = uint8_t(value);
  p[1] = uint8_t(value >> 8);
}

inline void position_put32(uint8_t *p, uint32_t value) {
  for (int i = 0; i != 4; i++)
    p[i] = uint8_t(value >> (8 * i));
}

struct PositionReport {
  static constexpr size_t SIZE = 24;
  static constexpr uint8_t TYPE = 'P';

  bool fix{false};
  bool flying{false};
  /// UTC seconds of the fix, 0 if the clock isn't set
  uint32_t time{0};
  double lat{0}, lon{0};
  /// GPS altitude above mean sea level
  int16_t alt_m{0};
  float speed_kmh{0};
  float course_deg{0};
  int16_t climb_cms{0};
  uint8_t satellites{0};
  /// The display's battery, or NAN if it has no reading
  float battery_v{NAN};

  size_t encode(uint8_t *p) const {
    p[0] = TYPE;
    p[1] = (this->fix ? 1 : 0) | (this->flying ? 2 : 0);
    position_put32(p + 2, this->time);
    position_put32(p + 6, static_cast<uint32_t>(static_cast<int32_t>(std::lround(this->lat * 1e7))));
    position_put32(p + 10, static_cast<uint32_t>(static_cast<int32_t>(std::lround(this->lon * 1e7))));
    position_put16(p + 14, static_cast<uint16_t>(this->alt_m));
    position_put16(p + 16, static_cast<uint16_t>(std::lround(this->speed_kmh * 10)));
    position_put16(p + 18, static_cast<uint16_t>(std::lround(this->course_deg * 10)));
    position_put16(p + 20, static_cast<uint16_t>(this->climb_cms));
    p[22] = this->satellites;
    // In 20 mV steps, up to 5.1 V; 0 means no reading
    p[23] = std::isnan(this->battery_v)
                ? 0
                : static_cast<uint8_t>(std::lround(std::min(5.1f, std::max(0.02f, this->battery_v)) * 50));
    return SIZE;
  }
};

}  // namespace esphome::tug_logger
