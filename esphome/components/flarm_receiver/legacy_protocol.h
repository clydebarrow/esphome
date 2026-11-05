#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::flarm_receiver {

/// The legacy FLARM radio protocol (Air V6 and V7 position packets), ported from SoftRF's Legacy.cpp:
/// Copyright (C) 2014-2015 Stanislaw Pusep, (C) 2016-2026 Linar Yusupov, GNU GPL v3 or later.

/// Manchester-coded bytes the radio delivers after the sync word: the 24-byte packet and its 2-byte CRC.
static constexpr size_t LEGACY_PACKET_SIZE = 24;
static constexpr size_t LEGACY_FRAME_SIZE = (LEGACY_PACKET_SIZE + 2) * 2;
/// The sync word, Manchester-coded, which the radio matches.
static constexpr uint8_t LEGACY_SYNC_WORD[8] = {0x55, 0x99, 0xA5, 0xA9, 0x55, 0x66, 0x65, 0x96};

/// The receiver's own state, needed to decrypt a packet and to place it.
struct OwnPosition {
  float latitude;
  float longitude;
  float geoid_separation;  // meters from the WGS84 ellipsoid up to mean sea level
  uint32_t timestamp;      // UTC, seconds since 1970
};

struct LegacyTarget {
  uint32_t address;
  uint8_t address_type;   // 0 random, 1 ICAO, 2 FLARM, 3 anonymous
  uint8_t aircraft_type;  // FLARM aircraft type, 0 to 15
  bool stealth;
  bool no_track;
  float latitude;
  float longitude;
  float altitude;    // meters above mean sea level
  float speed;       // meters per second
  float course;      // degrees true
  float climb_rate;  // meters per second
  float turn_rate;   // degrees per second, NAN if not sent
};

/// The sender's own state, for a packet to send.
struct OwnAircraft {
  uint32_t address;
  uint8_t address_type;   // 1 ICAO, 2 FLARM
  uint8_t aircraft_type;  // FLARM aircraft type, 0 to 15
  bool no_track;
  float latitude;
  float longitude;
  float altitude;          // meters above mean sea level
  float geoid_separation;  // meters from the WGS84 ellipsoid up to mean sea level
  float speed;             // meters per second
  float course;            // degrees true
  float climb_rate;        // meters per second
  uint32_t timestamp;      // UTC, seconds since 1970
};

/// Build an encrypted Air V7 position packet.
void legacy_encode(const OwnAircraft &own, uint8_t packet[LEGACY_PACKET_SIZE]);

/// Add the CRC to a packet and Manchester-code it into the frame the radio sends after the sync word.
void legacy_pack(const uint8_t packet[LEGACY_PACKET_SIZE], uint8_t frame[LEGACY_FRAME_SIZE]);

/// Remove the Manchester coding from a received frame and check its CRC; packet gets the 24 packet bytes.
bool legacy_unpack(const uint8_t *frame, size_t len, uint8_t packet[LEGACY_PACKET_SIZE]);

/// Decrypt and decode a packet in place. Returns false for packets that are not positions or fail the checks.
bool legacy_decode(uint8_t packet[LEGACY_PACKET_SIZE], const OwnPosition &own, LegacyTarget &target);

}  // namespace esphome::flarm_receiver
