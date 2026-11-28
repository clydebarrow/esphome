#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "identity_command.h"

namespace esphome::tracker_identity {

/**
 * The identity in its own flash page, as the tracker page writes it into a set-up UF2 for a board whose bootloader
 * takes UF2 files. Little-endian, the rest of the page erased:
 *
 *   0   magic "GID2"
 *   4   version, 16 bits
 *   6   LoRa address, 16 bits
 *   8   key, 32 bytes
 *   40  registration, NUL padded, 17 bytes
 *   57  padding, 3 bytes
 *   60  CRC-32 (IEEE) of bytes 0 to 59
 */
static constexpr size_t IDENTITY_PAGE_SIZE = 64;
static constexpr uint32_t IDENTITY_PAGE_MAGIC = 0x32444947;  // "GID2"
static constexpr uint16_t IDENTITY_PAGE_VERSION = 1;
static constexpr size_t IDENTITY_PAGE_KEY_OFFSET = 8;
static constexpr size_t IDENTITY_PAGE_REGISTRATION_OFFSET = 40;
static constexpr size_t IDENTITY_PAGE_CRC_OFFSET = 60;

inline uint32_t identity_page_crc(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i != length; i++) {
    crc ^= data[i];
    for (int bit = 0; bit != 8; bit++)
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

inline uint32_t identity_page_word(const uint8_t *data) {
  return uint32_t(data[0]) | uint32_t(data[1]) << 8 | uint32_t(data[2]) << 16 | uint32_t(data[3]) << 24;
}

/// Reads a page into \p identity; false for an erased page, or one that is not a whole, valid identity.
inline bool parse_identity_page(const uint8_t *page, Identity &identity) {
  if (identity_page_word(page) != IDENTITY_PAGE_MAGIC || (page[4] | page[5] << 8) != IDENTITY_PAGE_VERSION)
    return false;
  if (identity_page_word(page + IDENTITY_PAGE_CRC_OFFSET) != identity_page_crc(page, IDENTITY_PAGE_CRC_OFFSET))
    return false;
  const unsigned address = page[6] | page[7] << 8;
  if (address == 0 || address >= 0xFFFF)
    return false;
  const char *registration = reinterpret_cast<const char *>(page + IDENTITY_PAGE_REGISTRATION_OFFSET);
  const size_t length = strnlen(registration, REGISTRATION_SIZE + 1);
  if (length == 0 || length > REGISTRATION_SIZE)
    return false;
  for (size_t i = 0; i != length; i++) {
    if (!isalnum(static_cast<unsigned char>(registration[i])) && registration[i] != '-')
      return false;
  }
  Identity parsed;
  parsed.address = uint16_t(address);
  memcpy(parsed.key, page + IDENTITY_PAGE_KEY_OFFSET, KEY_SIZE);
  for (size_t i = 0; i != length; i++)
    parsed.registration[i] = char(toupper(static_cast<unsigned char>(registration[i])));
  identity = parsed;
  return true;
}

/// The page for \p identity, as the tracker page makes it
inline void make_identity_page(const Identity &identity, uint8_t *page) {
  memset(page, 0, IDENTITY_PAGE_SIZE);
  const uint32_t magic = IDENTITY_PAGE_MAGIC;
  for (int i = 0; i != 4; i++)
    page[i] = uint8_t(magic >> (8 * i));
  page[4] = uint8_t(IDENTITY_PAGE_VERSION);
  page[5] = uint8_t(IDENTITY_PAGE_VERSION >> 8);
  page[6] = uint8_t(identity.address);
  page[7] = uint8_t(identity.address >> 8);
  memcpy(page + IDENTITY_PAGE_KEY_OFFSET, identity.key, KEY_SIZE);
  memcpy(page + IDENTITY_PAGE_REGISTRATION_OFFSET, identity.registration,
         strnlen(identity.registration, REGISTRATION_SIZE));
  const uint32_t crc = identity_page_crc(page, IDENTITY_PAGE_CRC_OFFSET);
  for (int i = 0; i != 4; i++)
    page[IDENTITY_PAGE_CRC_OFFSET + i] = uint8_t(crc >> (8 * i));
}

}  // namespace esphome::tracker_identity
