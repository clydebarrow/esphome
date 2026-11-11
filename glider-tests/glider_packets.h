#pragma once
// Bench packet formats for the glider logger. All fields are little endian. The addressing layer adds the 4-byte
// destination and source header in front of each, so the sender is known from the packet metadata.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace glider {

enum PacketType : uint8_t {
  POSITION = 'P',  // broadcast, unencrypted, while flying (on the bench, always)
  SUMMARY = 'S',   // to the clubhouse, encrypted, until acknowledged
  ACK = 'A',       // from the clubhouse, encrypted: the summary with this takeoff time is stored
};

inline void put16(uint8_t *p, uint16_t v) {
  p[0] = v;
  p[1] = v >> 8;
}
inline void put32(uint8_t *p, uint32_t v) {
  for (int i = 0; i != 4; i++)
    p[i] = v >> (8 * i);
}
inline uint16_t get16(const uint8_t *p) { return p[0] | (p[1] << 8); }
inline uint32_t get32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

/// A position report: 24 bytes.
struct Position {
  static constexpr size_t SIZE = 24;
  bool fix{false};
  bool flying{false};
  uint32_t time{0};       // Unix time of the fix, 0 if the clock isn't set
  double lat{0}, lon{0};  // degrees
  int16_t alt_m{0};       // GPS altitude above mean sea level
  float speed_kmh{0};
  float course_deg{0};
  int16_t climb_cms{0};
  uint8_t satellites{0};
  float battery_v{NAN};

  size_t encode(uint8_t *p) const {
    p[0] = POSITION;
    p[1] = (fix ? 1 : 0) | (flying ? 2 : 0);
    put32(p + 2, time);
    put32(p + 6, static_cast<uint32_t>(static_cast<int32_t>(std::lround(lat * 1e7))));
    put32(p + 10, static_cast<uint32_t>(static_cast<int32_t>(std::lround(lon * 1e7))));
    put16(p + 14, static_cast<uint16_t>(alt_m));
    put16(p + 16, static_cast<uint16_t>(std::lround(speed_kmh * 10)));
    put16(p + 18, static_cast<uint16_t>(std::lround(course_deg * 10)));
    put16(p + 20, static_cast<uint16_t>(climb_cms));
    p[22] = satellites;
    p[23] = std::isnan(battery_v) ? 0 : static_cast<uint8_t>(std::lround(battery_v * 50));  // 20 mV steps
    return SIZE;
  }
  bool decode(const uint8_t *p, size_t len) {
    if (len != SIZE || p[0] != POSITION)
      return false;
    fix = p[1] & 1;
    flying = p[1] & 2;
    time = get32(p + 2);
    lat = static_cast<int32_t>(get32(p + 6)) / 1e7;
    lon = static_cast<int32_t>(get32(p + 10)) / 1e7;
    alt_m = static_cast<int16_t>(get16(p + 14));
    speed_kmh = get16(p + 16) / 10.0f;
    course_deg = get16(p + 18) / 10.0f;
    climb_cms = static_cast<int16_t>(get16(p + 20));
    satellites = p[22];
    battery_v = p[23] == 0 ? NAN : p[23] / 50.0f;
    return true;
  }
};

/// A flight summary: 29 bytes. Identified by its takeoff time, so sending it again is always safe.
struct Summary {
  static constexpr size_t SIZE = 29;
  uint32_t takeoff{0}, landing{0};  // Unix time
  double takeoff_lat{0}, takeoff_lon{0}, landing_lat{0}, landing_lon{0};
  int16_t takeoff_alt_m{0}, landing_alt_m{0};

  size_t encode(uint8_t *p) const {
    p[0] = SUMMARY;
    put32(p + 1, takeoff);
    put32(p + 5, landing);
    put32(p + 9, static_cast<uint32_t>(static_cast<int32_t>(std::lround(takeoff_lat * 1e7))));
    put32(p + 13, static_cast<uint32_t>(static_cast<int32_t>(std::lround(takeoff_lon * 1e7))));
    put32(p + 17, static_cast<uint32_t>(static_cast<int32_t>(std::lround(landing_lat * 1e7))));
    put32(p + 21, static_cast<uint32_t>(static_cast<int32_t>(std::lround(landing_lon * 1e7))));
    put16(p + 25, static_cast<uint16_t>(takeoff_alt_m));
    put16(p + 27, static_cast<uint16_t>(landing_alt_m));
    return SIZE;
  }
  bool decode(const uint8_t *p, size_t len) {
    if (len != SIZE || p[0] != SUMMARY)
      return false;
    takeoff = get32(p + 1);
    landing = get32(p + 5);
    takeoff_lat = static_cast<int32_t>(get32(p + 9)) / 1e7;
    takeoff_lon = static_cast<int32_t>(get32(p + 13)) / 1e7;
    landing_lat = static_cast<int32_t>(get32(p + 17)) / 1e7;
    landing_lon = static_cast<int32_t>(get32(p + 21)) / 1e7;
    takeoff_alt_m = static_cast<int16_t>(get16(p + 25));
    landing_alt_m = static_cast<int16_t>(get16(p + 27));
    return true;
  }
};

/// An acknowledgement: 5 bytes.
struct Ack {
  static constexpr size_t SIZE = 5;
  uint32_t takeoff{0};

  size_t encode(uint8_t *p) const {
    p[0] = ACK;
    put32(p + 1, takeoff);
    return SIZE;
  }
  bool decode(const uint8_t *p, size_t len) {
    if (len != SIZE || p[0] != ACK)
      return false;
    takeoff = get32(p + 1);
    return true;
  }
};

}  // namespace glider
