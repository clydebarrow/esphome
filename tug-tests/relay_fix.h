#pragma once
// A GPS fix from the tug's LoRa relay (a Heltec Wireless Tracker, with its own GNSS) to the tug's display, over the
// UART link between them. The display uses it while the SkyEcho gives none. It goes from the relay's own address to
// broadcast, unencrypted, as the tug's address comes from its tug file; the relay never sends it over LoRa.
// Used by tug-relay-tracker.yaml and tug-wave8-lora.yaml.

#include <cstddef>
#include <cstdint>

namespace relay_fix {

/// The relay's address on the link; the display has a peer for it
static constexpr uint16_t RELAY_ADDRESS = 0x00FE;
static constexpr uint8_t TYPE = 'G';
/// From the display to the relay's address: a packet of this one byte asks the relay to turn its Wi-Fi on for 5
/// minutes and check for firmware (tug-relay-tracker.yaml). tug-relay-common.yaml takes it off the link.
static constexpr uint8_t WIFI_REQUEST = 'W';

enum Flags : uint8_t {
  FLAG_LOCATION = 1,
  FLAG_SPEED = 2,
  FLAG_ALTITUDE = 4,
  FLAG_TIME = 8,
  FLAG_COURSE = 16,
};

inline void put32(uint8_t *out, uint32_t value) {
  for (int i = 0; i != 4; i++)
    out[i] = uint8_t(value >> (8 * i));
}

inline uint32_t get32(const uint8_t *in) {
  return uint32_t(in[0]) | (uint32_t(in[1]) << 8) | (uint32_t(in[2]) << 16) | (uint32_t(in[3]) << 24);
}

struct Fix {
  static constexpr size_t SIZE = 23;
  /// Before the course was added; still accepted, with no course
  static constexpr size_t OLD_SIZE = 21;

  uint8_t flags{0};
  int32_t lat_e7{0};
  int32_t lon_e7{0};
  int32_t altitude_cm{0};
  uint16_t speed_cms{0};
  /// UTC seconds, when FLAG_TIME is set
  uint32_t time{0};
  uint8_t satellites{0};
  /// Course over the ground in hundredths of a degree, when FLAG_COURSE is set
  uint16_t course_cdeg{0};

  void encode(uint8_t *out) const {
    out[0] = TYPE;
    out[1] = this->flags;
    put32(out + 2, uint32_t(this->lat_e7));
    put32(out + 6, uint32_t(this->lon_e7));
    put32(out + 10, uint32_t(this->altitude_cm));
    out[14] = uint8_t(this->speed_cms);
    out[15] = uint8_t(this->speed_cms >> 8);
    put32(out + 16, this->time);
    out[20] = this->satellites;
    out[21] = uint8_t(this->course_cdeg);
    out[22] = uint8_t(this->course_cdeg >> 8);
  }

  bool decode(const uint8_t *in, size_t length) {
    if ((length != SIZE && length != OLD_SIZE) || in[0] != TYPE)
      return false;
    this->flags = in[1];
    this->lat_e7 = int32_t(get32(in + 2));
    this->lon_e7 = int32_t(get32(in + 6));
    this->altitude_cm = int32_t(get32(in + 10));
    this->speed_cms = uint16_t(in[14] | (in[15] << 8));
    this->time = get32(in + 16);
    this->satellites = in[20];
    if (length == SIZE) {
      this->course_cdeg = uint16_t(in[21] | (in[22] << 8));
    } else {
      this->flags &= ~FLAG_COURSE;
      this->course_cdeg = 0;
    }
    return true;
  }
};

}  // namespace relay_fix
