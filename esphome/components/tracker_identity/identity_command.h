#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace esphome::tracker_identity {

/// As the LoRa address registry allows: letters, digits and hyphens
static constexpr size_t REGISTRATION_SIZE = 16;
static constexpr size_t KEY_SIZE = 32;

struct Identity {
  uint16_t address{0};
  uint8_t key[KEY_SIZE]{};
  char registration[REGISTRATION_SIZE + 1]{};
};

inline int hex_digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/// Parses the arguments of "SET <registration> <address in hex> <64 hex digits of key>" into \p identity. Returns
/// nullptr on success, or the reason to answer with.
inline const char *parse_set(const std::string &args, Identity &identity) {
  // Three words, split on spaces
  std::string words[3];
  size_t count = 0;
  for (size_t i = 0; i < args.size();) {
    if (args[i] == ' ') {
      i++;
      continue;
    }
    const size_t space = args.find(' ', i);
    const size_t stop = space == std::string::npos ? args.size() : space;
    if (count == 3)
      return "usage: SET <registration> <address> <key>";
    words[count++] = args.substr(i, stop - i);
    i = stop;
  }
  if (count != 3)
    return "usage: SET <registration> <address> <key>";
  const std::string &registration = words[0], &address_text = words[1], &key_text = words[2];
  const size_t length = registration.size();
  bool ok = length != 0 && length <= REGISTRATION_SIZE;
  for (size_t i = 0; i != length && ok; i++)
    ok = isalnum(static_cast<unsigned char>(registration[i])) || registration[i] == '-';
  if (!ok)
    return "the registration must be 1 to 16 letters, digits and hyphens";
  if (address_text.empty() || address_text.size() > 4)
    return "the address must be 0001 to FFFE in hex";
  unsigned long address = 0;
  for (char c : address_text) {
    const int digit = hex_digit(c);
    if (digit < 0)
      return "the address must be 0001 to FFFE in hex";
    address = address << 4 | unsigned(digit);
  }
  if (address == 0 || address >= 0xFFFF)
    return "the address must be 0001 to FFFE in hex";
  if (key_text.size() != KEY_SIZE * 2)
    return "the key must be 64 hex digits";
  Identity parsed;
  for (size_t i = 0; i != KEY_SIZE; i++) {
    const int high = hex_digit(key_text[2 * i]), low = hex_digit(key_text[2 * i + 1]);
    if (high < 0 || low < 0)
      return "the key must be 64 hex digits";
    parsed.key[i] = uint8_t(high << 4 | low);
  }
  parsed.address = uint16_t(address);
  for (size_t i = 0; i != length; i++)
    parsed.registration[i] = char(toupper(static_cast<unsigned char>(registration[i])));
  identity = parsed;
  return nullptr;
}

/// The answer to "ID". The registration is only letters, digits and hyphens, so it needs no escaping.
inline std::string id_line(const char *project, uint32_t build, const Identity *identity, const char *mac) {
  char line[200];
  snprintf(line, sizeof(line),
           "ID {\"project\":\"%s\",\"build\":%lu,\"set\":%s,\"address\":%u,\"registration\":\"%s\",\"mac\":\"%s\"}\r\n",
           project, (unsigned long) build, identity != nullptr ? "true" : "false",
           identity != nullptr ? unsigned(identity->address) : 0u, identity != nullptr ? identity->registration : "",
           mac);
  return line;
}

}  // namespace esphome::tracker_identity
