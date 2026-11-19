#pragma once
// Firmware updates for the tug's LoRa relay from the flight-upload Worker, as the tug's display and the clubhouse
// receiver have: a check signed with the relay's device key, and the Worker's offer of a newer build.
// Used by tug-relay-tracker.yaml.

#include <cstdint>
#include <cstdio>
#include <string>

#include "esp_app_desc.h"
#include "mbedtls/md.h"

namespace relay_firmware {

/// The check in progress, and the offer it brought back
struct Check {
  bool busy{false};
  std::string body;
  std::string signature;
  std::string url;
  std::string md5;
};
inline Check check;

/// The X-Signature header: sha256= and the hex HMAC-SHA256 of the body, keyed with the UTF-8 bytes of the secret
inline std::string sign(const std::string &secret, const std::string &body) {
  uint8_t mac[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const uint8_t *>(secret.data()),
                  secret.size(), reinterpret_cast<const uint8_t *>(body.data()), body.size(), mac);
  std::string out = "sha256=";
  char hex[3];
  for (uint8_t b : mac) {
    snprintf(hex, sizeof(hex), "%02x", b);
    out += hex;
  }
  return out;
}

/// The check's body: this build's project (the device name), ELF hash and build time. The name has only letters,
/// digits and hyphens, so it needs no escaping.
inline std::string check_json(int64_t build_time) {
  char elf_sha256[65];
  esp_app_get_elf_sha256(elf_sha256, sizeof(elf_sha256));
  char built[24];
  snprintf(built, sizeof(built), "%lld", (long long) build_time);
  return std::string("{\"project\":\"") + esp_app_get_description()->project_name + "\",\"elf_sha256\":\"" +
         elf_sha256 + "\",\"build_time\":" + built + "}";
}

}  // namespace relay_firmware
