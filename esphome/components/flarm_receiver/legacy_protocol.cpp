#include "legacy_protocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "esphome/core/helpers.h"

namespace esphome::flarm_receiver {

static constexpr uint32_t DELTA = 0x9e3779b9;
static constexpr uint32_t V6_KEY_TABLE[8] = {0xe43276df, 0xdca83759, 0x9802b8ac, 0x4675a56b,
                                             0xfc78ea65, 0x804b90ea, 0xb76542cd, 0x329dfa32};
static constexpr uint32_t V6_KEY2 = 0x045d9f3b;
static constexpr uint32_t V6_KEY3 = 0x87b562f4;
static constexpr uint32_t V7_KEY4 = 0x956F6C77;
static constexpr uint32_t V7_XXTEA_KEY[4] = {0xA5F9B21C, 0xAB3F9D12, 0xC6F34E34, 0xD72FA378};
// The nRF905 address bytes that the CRC starts with
static constexpr uint8_t CRC_ADDRESS[3] = {0x31, 0xFA, 0xB6};
// Longitude scale for latitudes of 14 degrees and up, by whole degree
static constexpr uint16_t V7_LON_DIV[] = {
    53,  53,  54,  54,  55,  55,  56,  56,  57,  57,  58,  58,  59,  59,  60,  60,  61,  61,  62,  62,
    63,  63,  64,  64,  65,  65,  67,  68,  70,  71,  73,  74,  76,  77,  79,  80,  82,  83,  85,  86,
    88,  89,  91,  94,  98,  101, 105, 108, 112, 115, 119, 122, 126, 129, 137, 144, 152, 159, 167, 174,
    190, 205, 221, 236, 252, 267, 299, 330, 362, 425, 489, 552, 616, 679, 743, 806, 806};

// The packets are little-endian bit fields: bit n is bit n % 8 of byte n / 8
static uint32_t get_bits(const uint8_t *data, unsigned offset, unsigned len) {
  uint32_t value = 0;
  for (unsigned i = 0; i != len; i++) {
    const unsigned bit = offset + i;
    value |= static_cast<uint32_t>((data[bit / 8] >> (bit % 8)) & 1) << i;
  }
  return value;
}

static int32_t get_signed_bits(const uint8_t *data, unsigned offset, unsigned len) {
  const uint32_t value = get_bits(data, offset, len);
  return (value & (1u << (len - 1))) ? static_cast<int32_t>(value) - (1 << len) : static_cast<int32_t>(value);
}

// XXTEA decryption with 6 rounds (https://en.wikipedia.org/wiki/XXTEA)
static void btea_decrypt(uint32_t *v, unsigned n, const uint32_t key[4]) {
  uint32_t sum = 6 * DELTA;
  uint32_t y = v[0];
  uint32_t z;
  for (unsigned rounds = 6; rounds != 0; rounds--) {
    const uint32_t e = (sum >> 2) & 3;
    unsigned p;
    for (p = n - 1; p > 0; p--) {
      z = v[p - 1];
      y = v[p] -= (((z >> 5 ^ y << 2) + (y >> 3 ^ z << 4)) ^ ((sum ^ y) + (key[(p & 3) ^ e] ^ z)));
    }
    z = v[n - 1];
    y = v[0] -= (((z >> 5 ^ y << 2) + (y >> 3 ^ z << 4)) ^ ((sum ^ y) + (key[(p & 3) ^ e] ^ z)));
    sum -= DELTA;
  }
}

static uint32_t obscure(uint32_t key, uint32_t seed) {
  const uint32_t m1 = seed * (key ^ (key >> 16));
  const uint32_t m2 = seed * (m1 ^ (m1 >> 16));
  return m2 ^ (m2 >> 16);
}

static void make_v6_key(uint32_t key[4], uint32_t timestamp, uint32_t address) {
  for (unsigned i = 0; i != 4; i++) {
    const unsigned ndx = ((timestamp >> 23) & 1) ? i + 4 : i;
    key[i] = obscure(V6_KEY_TABLE[ndx] ^ ((timestamp >> 6) ^ address), V6_KEY2) ^ V6_KEY3;
  }
}

static void make_v7_key(uint32_t key[4]) {
  uint8_t bkeys[16];
  std::memcpy(bkeys, key, sizeof(bkeys));
  // Unsigned: SoftRF's int version relies on signed overflow
  uint32_t x = bkeys[15];
  uint32_t sum = 0;
  for (int q = 2; q > 0; q--) {
    sum += DELTA;
    for (unsigned p = 0; p != 16; p++) {
      const uint32_t z = x & 0xFF;
      const uint32_t y = bkeys[(p + 1) % 16];
      x = bkeys[p];
      x += ((((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^ (sum ^ y));
      bkeys[p] = static_cast<uint8_t>(x);
    }
  }
  std::memcpy(key, bkeys, sizeof(bkeys));
}

// A number with mbits of mantissa and ebits of exponent, and a sign bit above them if is_signed
static int descale(uint32_t value, unsigned mbits, unsigned ebits, bool is_signed) {
  const uint32_t offset = 1u << mbits;
  const uint32_t signbit = offset << ebits;
  const bool negative = is_signed && (value & signbit);
  value &= signbit - 1;
  if (value >= offset) {
    const uint32_t exp = value >> mbits;
    value &= offset - 1;
    value += offset;
    value <<= exp;
    value -= offset;
  }
  return negative ? -static_cast<int>(value) : static_cast<int>(value);
}

// The full coordinate nearest the reference whose low bits are the packet's
static int32_t nearest(uint32_t packet_value, int32_t reference, uint32_t modulus) {
  int32_t diff = static_cast<int32_t>((packet_value - static_cast<uint32_t>(reference)) % modulus);
  if (diff >= static_cast<int32_t>(modulus / 2))
    diff -= static_cast<int32_t>(modulus);
  return reference + diff;
}

bool legacy_unpack(const uint8_t *frame, size_t len, uint8_t packet[LEGACY_PACKET_SIZE]) {
  if (len < LEGACY_FRAME_SIZE)
    return false;
  uint8_t bytes[LEGACY_PACKET_SIZE + 2];
  for (size_t i = 0; i != sizeof(bytes); i++) {
    uint8_t value = 0;
    uint16_t chips = (frame[i * 2] << 8) | frame[i * 2 + 1];
    // Each bit is two chips, most significant first: 10 for a 0, 01 for a 1
    for (unsigned bit = 0; bit != 8; bit++) {
      const unsigned pair = (chips >> (14 - bit * 2)) & 3;
      if (pair == 1) {
        value |= 0x80 >> bit;
      } else if (pair != 2) {
        return false;
      }
    }
    bytes[i] = value;
  }
  uint16_t crc = crc16be(CRC_ADDRESS, sizeof(CRC_ADDRESS), 0xFFFF);
  crc = crc16be(bytes, LEGACY_PACKET_SIZE, crc);
  if (crc != encode_uint16(bytes[LEGACY_PACKET_SIZE], bytes[LEGACY_PACKET_SIZE + 1]))
    return false;
  std::memcpy(packet, bytes, LEGACY_PACKET_SIZE);
  return true;
}

static bool decode_v6(uint8_t *packet, const OwnPosition &own, LegacyTarget &target) {
  uint32_t words[6];
  std::memcpy(words, packet, sizeof(words));
  uint32_t key[4];
  make_v6_key(key, own.timestamp, (get_bits(packet, 0, 24) << 8) & 0xFFFFFF);
  btea_decrypt(words + 1, 5, key);
  std::memcpy(packet, words, sizeof(words));

  unsigned parity = 0;
  for (size_t i = 0; i != LEGACY_PACKET_SIZE; i++)
    parity += __builtin_popcount(packet[i]);
  if (parity % 2)
    return false;

  const int32_t round_lat = static_cast<int32_t>(own.latitude * 1e7f) >> 7;
  const int32_t lat = nearest(get_bits(packet, 64, 19), round_lat, 0x80000) << 7;
  const int32_t round_lon = static_cast<int32_t>(own.longitude * 1e7f) >> 7;
  const int32_t lon = nearest(get_bits(packet, 96, 20), round_lon, 0x100000) << 7;

  const unsigned smult = get_bits(packet, 126, 2);
  const auto *ns = reinterpret_cast<const int8_t *>(packet + 16);
  const auto *ew = reinterpret_cast<const int8_t *>(packet + 20);
  const int north = (ns[0] + ns[1] + ns[2] + ns[3]) / 4;
  const int east = (ew[0] + ew[1] + ew[2] + ew[3]) / 4;

  target.latitude = lat / 1e7f;
  target.longitude = lon / 1e7f;
  target.altitude = static_cast<float>(get_bits(packet, 83, 13)) - own.geoid_separation;
  target.speed = std::sqrt(static_cast<float>(north * north + east * east)) * (1 << smult) / 4.0f;
  float course = std::atan2(static_cast<float>(east), static_cast<float>(north)) * 180.0f / static_cast<float>(M_PI);
  target.course = course < 0 ? course + 360.0f : course;
  target.climb_rate = static_cast<float>(get_signed_bits(packet, 32, 10) * (1 << smult)) / 10.0f;
  target.turn_rate = NAN;
  target.stealth = get_bits(packet, 45, 1);
  target.no_track = get_bits(packet, 46, 1);
  target.aircraft_type = get_bits(packet, 60, 4);
  return true;
}

static void decrypt_v7(uint8_t *packet, uint32_t timestamp) {
  uint32_t words[6];
  std::memcpy(words, packet, sizeof(words));
  btea_decrypt(words + 2, 4, V7_XXTEA_KEY);
  uint32_t key[4] = {words[0], words[1], timestamp >> 4, V7_KEY4};
  make_v7_key(key);
  for (unsigned i = 0; i != 4; i++)
    words[i + 2] ^= key[i];
  std::memcpy(packet, words, sizeof(words));
}

static bool decode_v7(uint8_t *packet, const OwnPosition &own, LegacyTarget &target) {
  // The key changes every 16 seconds; the packet's time stamp (its low 4 bits) shows whether the sender was in the
  // same 16 seconds, and if not, the neighboring 16 seconds are tried.
  uint8_t copy[LEGACY_PACKET_SIZE];
  std::memcpy(copy, packet, sizeof(copy));
  decrypt_v7(packet, own.timestamp);
  int diff = (static_cast<int>(get_bits(packet, 66, 4)) - static_cast<int>(own.timestamp & 0xF) + 16) % 16;
  if (diff > 8)
    diff -= 16;
  if (diff < -2 || diff > 2)
    return false;
  if (((own.timestamp + diff) >> 4) != (own.timestamp >> 4)) {
    std::memcpy(packet, copy, sizeof(copy));
    decrypt_v7(packet, own.timestamp + diff);
  }

  target.stealth = get_bits(packet, 54, 1);
  target.no_track = get_bits(packet, 55, 1);
  target.aircraft_type = std::min<uint32_t>(get_bits(packet, 70, 5), 15);
  target.altitude = static_cast<float>(descale(get_bits(packet, 75, 13), 12, 1, false) - 1000) - own.geoid_separation;

  const int32_t round_lat = static_cast<int32_t>(own.latitude * 1e7f) / 52;
  const int32_t lat = nearest(get_bits(packet, 88, 20), round_lat, 0x100000) * 52;
  target.latitude = lat / 1e7f;
  int ilat = std::min(static_cast<int>(std::fabs(target.latitude)), 89);
  const int32_t lon_div = ilat < 14 ? 52 : V7_LON_DIV[ilat - 14];
  const int32_t round_lon = static_cast<int32_t>(own.longitude * 1e7f) / lon_div;
  const int32_t lon = nearest(get_bits(packet, 108, 20), round_lon, 0x100000) * lon_div;
  target.longitude = lon / 1e7f;

  target.turn_rate = static_cast<float>(descale(get_bits(packet, 128, 9), 6, 2, true)) / 20.0f;
  target.speed = static_cast<float>(descale(get_bits(packet, 137, 10), 8, 2, false)) / 10.0f;
  target.climb_rate = static_cast<float>(descale(get_bits(packet, 147, 9), 6, 2, true)) / 10.0f;
  target.course = static_cast<float>(get_bits(packet, 156, 10)) / 2.0f;
  return true;
}

bool legacy_decode(uint8_t packet[LEGACY_PACKET_SIZE], const OwnPosition &own, LegacyTarget &target) {
  target.address = get_bits(packet, 0, 24);
  const unsigned type = get_bits(packet, 24, 4);
  if (type == 0) {
    target.address_type = get_bits(packet, 28, 3);
    return decode_v6(packet, own, target);
  }
  if (type == 2) {
    target.address_type = get_bits(packet, 28, 2);
    return decode_v7(packet, own, target);
  }
  return false;
}

}  // namespace esphome::flarm_receiver
