#pragma once
// Upload of flight summaries from the clubhouse receiver to the flight-upload Worker (POST /api/flights).
//
// The receiver keeps no queue: each glider keeps its summary in flash and sends it every 10 s until acknowledged, so
// the receiver posts each summary it hears and acknowledges it only once the Worker has answered. One upload runs at
// a time; a summary heard meanwhile is ignored, and comes again.
//
// Positions are collected as they are heard and posted in batches to /api/positions for the live map. A batch that
// fails is dropped: a position is soon out of date anyway.
//
// Devices: the receiver asks the Worker (POST /api/receiver/devices) for the gliders and tugs it serves, each with
// its LoRa address and key, at every clock sync (so soon after Wi-Fi connects, then every 15 minutes), and gives each
// a slot in the gliders or tugs pool. Nothing is kept in flash: without Wi-Fi it could not post anything anyway.
//
// Firmware: the receiver asks the Worker (POST /api/firmware/check, as a tug does) whether there is a build for it
// other than the one it runs, and installs it over HTTP, so a receiver on a pole can be updated from anywhere.

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <sys/time.h>
#include <functional>
#include <string>
#include <vector>

#include "esp_app_desc.h"
#include "mbedtls/md.h"

#include "glider_packets.h"

namespace receiver {

/// A glider the receiver has a stack for. ack sends the acknowledgement for a takeoff time on that stack.
struct Glider {
  uint16_t address{0};
  std::string name;
  std::string registration;
  std::string type;
  std::function<bool(uint32_t)> ack;
};

/// The gliders from the Worker, one for each slot of the gliders pool (address 0 for a free slot)
inline std::vector<Glider> pool_gliders;

/// The upload in progress
struct Upload {
  bool busy{false};
  /// A copy, as the slot may be given to another glider while the upload runs
  Glider glider;
  uint32_t takeoff{0};
  std::string body;
  std::string signature;
};

inline Upload upload;

/// Positions waiting to be posted, each as a JSON object, and the post in progress
struct Positions {
  static constexpr size_t MAX_WAITING = 40;
  std::vector<std::string> waiting;
  bool busy{false};
  std::string body;
  std::string signature;
};

inline Positions positions;

/// A tug the receiver has a stack for. Its tows are posted with the tug's own signature, so the Worker checks the
/// tug's key; answer sends the Worker's status back on that stack. The packets are those of tow_link.h in the
/// tug_logger component: a tow is 'T', the 32-byte HMAC-SHA256 of the body, then the body; an answer is 'K', the
/// first 4 bytes of that HMAC, then the 16-bit status, or 0 if the Worker could not be reached.
struct Tug {
  uint16_t address{0};
  std::string name;
  std::function<bool(const uint8_t *, size_t)> send;
};

/// As for gliders, one for each slot of the tugs pool
inline std::vector<Tug> pool_tugs;

/// The tow upload in progress
struct TowUpload {
  static constexpr size_t MAC_SIZE = 32;
  static constexpr size_t TAG_SIZE = 4;
  static constexpr size_t ANSWER_SIZE = 1 + TAG_SIZE + 2;
  bool busy{false};
  Tug tug;
  uint8_t tag[TAG_SIZE]{};
  std::string body;
  std::string signature;
};

inline TowUpload tow_upload;

/// Takes a tow packet apart into the body and the X-Signature header; false if it isn't one
inline bool decode_tow(const uint8_t *p, size_t len, TowUpload &out) {
  if (len <= 1 + TowUpload::MAC_SIZE || p[0] != 'T')
    return false;
  out.signature = "sha256=";
  char hex[3];
  for (size_t i = 0; i != TowUpload::MAC_SIZE; i++) {
    snprintf(hex, sizeof(hex), "%02x", p[1 + i]);
    out.signature += hex;
  }
  memcpy(out.tag, p + 1, TowUpload::TAG_SIZE);
  out.body.assign(reinterpret_cast<const char *>(p + 1 + TowUpload::MAC_SIZE), len - 1 - TowUpload::MAC_SIZE);
  return true;
}

/// Sends the Worker's status, or 0 for no answer, back to the tug of the upload in progress
inline bool answer_tow(int status) {
  uint8_t out[TowUpload::ANSWER_SIZE];
  out[0] = 'K';
  memcpy(out + 1, tow_upload.tag, TowUpload::TAG_SIZE);
  glider::put16(out + 1 + TowUpload::TAG_SIZE, static_cast<uint16_t>(status < 0 ? 0 : status));
  return tow_upload.tug.send(out, sizeof(out));
}

inline const Tug *find_tug(uint16_t address) {
  for (const auto &tug : pool_tugs) {
    if (tug.address == address && address != 0)
      return &tug;
  }
  return nullptr;
}

inline const Glider *find_glider(uint16_t address) {
  for (const auto &glider : pool_gliders) {
    if (glider.address == address && address != 0)
      return &glider;
  }
  return nullptr;
}

/// UTC time as the Worker wants it: YYYY-MM-DDTHH:MM:SSZ
inline std::string iso_time(uint32_t unix_time) {
  time_t t = unix_time;
  struct tm tm{};
  gmtime_r(&t, &tm);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

/// Escapes a string for JSON; the names come from the config, so this only guards against quotes and backslashes
inline std::string json_string(const char *text) {
  std::string out = "\"";
  for (const char *p = text; *p != 0; p++) {
    if (*p == '"' || *p == '\\')
      out += '\\';
    out += *p;
  }
  return out + "\"";
}
inline std::string json_string(const std::string &text) { return json_string(text.c_str()); }

/// The flight record. The pilot is left blank, which the Worker stores as Unknown.
inline std::string flight_json(const char *organisation, const Glider &glider, const glider::Summary &summary) {
  char duration[11];
  snprintf(duration, sizeof(duration), "%" PRIu32, summary.landing - summary.takeoff);
  return "{\"organisation\":" + json_string(organisation) + ",\"glider_type\":" + json_string(glider.type) +
         ",\"registration\":" + json_string(glider.registration) + ",\"pilot\":\"\",\"takeoff\":\"" +
         iso_time(summary.takeoff) + "\",\"landing\":\"" + iso_time(summary.landing) + "\",\"duration\":" + duration +
         "}";
}

/// One position for the live map. Fields the glider doesn't know without a fix are left out.
/// For a tug, \p tug is set, and the Worker shows it as one: tugs send the same position packet as gliders
inline std::string position_json(const std::string &registration, bool tug, const glider::Position &pos, float rssi,
                                 float snr) {
  char buf[340];
  int n = snprintf(buf, sizeof(buf), "{\"registration\":%s,%s\"time\":%u,\"fix\":%s,\"flying\":%s,\"satellites\":%u",
                   json_string(registration).c_str(), tug ? "\"kind\":\"tug\"," : "", (unsigned) pos.time,
                   pos.fix ? "true" : "false", pos.flying ? "true" : "false", (unsigned) pos.satellites);
  if (pos.fix) {
    n += snprintf(buf + n, sizeof(buf) - n,
                  ",\"lat\":%.6f,\"lon\":%.6f,\"alt_m\":%d,\"speed_kmh\":%.1f,\"course_deg\":%.1f,\"climb_ms\":%.2f",
                  pos.lat, pos.lon, pos.alt_m, pos.speed_kmh, pos.course_deg, pos.climb_cms / 100.0f);
  }
  if (!std::isnan(pos.battery_v))
    n += snprintf(buf + n, sizeof(buf) - n, ",\"battery_v\":%.2f", pos.battery_v);
  snprintf(buf + n, sizeof(buf) - n, ",\"rssi\":%.0f,\"snr\":%.1f}", rssi, snr);
  return buf;
}

/// Queues a position, dropping the oldest if too many are waiting
inline void queue_position(std::string json) {
  if (positions.waiting.size() >= Positions::MAX_WAITING)
    positions.waiting.erase(positions.waiting.begin());
  positions.waiting.push_back(std::move(json));
}

/// Takes the waiting positions as the body of a batch
inline std::string take_positions() {
  std::string body = "{\"positions\":[";
  for (size_t i = 0; i != positions.waiting.size(); i++) {
    if (i != 0)
      body += ',';
    body += positions.waiting[i];
  }
  positions.waiting.clear();
  return body + "]}";
}

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

/// Whether a status means the glider can forget the flight: it is stored (201, or 200 for a duplicate), or the
/// Worker will never take it (400 for a record it can't use, 409 for a different record with the same takeoff),
/// so sending it again would only use airtime. 401, 5xx and network errors are worth another try.
inline bool settled(int status) { return status == 200 || status == 201 || status == 400 || status == 409; }

/// The firmware check: one at a time, and the offer it brought back.
struct FirmwareCheck {
  bool busy{false};
  std::string body;
  std::string signature;
  std::string url;
  std::string md5;
};
inline FirmwareCheck firmware;

/// The request for the device list: signed, with the time so an old request can't be sent again
struct DevicesFetch {
  bool busy{false};
  /// The clock was set from the Worker's answer to a request it refused for the time, so ask again
  bool retry{false};
  std::string body;
  std::string signature;
};
inline DevicesFetch devices;

/**
 * Gives each device in \p list (the Worker's "gliders" or "tugs") a slot in \p pool, and frees the slots of devices
 * no longer listed. \p fill sets an entry's names from its JSON. Returns how many have slots, and adds what changed to
 * \p notes, for the caller to log (a header can't).
 */
template<typename Entry, typename Fill>
size_t apply_device_list(esphome::packet_interface::PeerPool &pool, std::vector<Entry> &entries, JsonArray list,
                         const char *kind, Fill fill, std::vector<std::string> &notes) {
  char note[96];
  std::vector<uint16_t> listed;
  for (JsonObject device : list)
    listed.push_back(device["address"].as<uint16_t>());
  for (size_t slot = 0; slot != pool.size(); slot++) {
    uint16_t address = pool.address(slot);
    if (address != 0 && std::find(listed.begin(), listed.end(), address) == listed.end()) {
      snprintf(note, sizeof(note), "%s 0x%04X (%s) is no longer listed", kind, address, entries[slot].name.c_str());
      notes.emplace_back(note);
      pool.release(address);
      entries[slot].address = 0;
    }
  }
  size_t count = 0;
  for (JsonObject device : list) {
    uint16_t address = device["address"].as<uint16_t>();
    uint8_t key[32];
    if (!esphome::parse_hex(device["key"].as<std::string>(), key, sizeof(key))) {
      snprintf(note, sizeof(note), "%s 0x%04X has no usable key", kind, address);
      notes.emplace_back(note);
      continue;
    }
    int slot = pool.assign(address, key);
    if (slot < 0)
      continue;
    Entry &entry = entries[slot];
    bool added = entry.address != address;
    entry.address = address;
    fill(entry, device);
    if (added) {
      snprintf(note, sizeof(note), "%s 0x%04X is %s, in slot %d", kind, address, entry.name.c_str(), slot);
      notes.emplace_back(note);
    }
    count++;
  }
  return count;
}

/// Whether the clock has been set, by SNTP, Home Assistant or the Worker's Date header
inline bool clock_set() { return ::time(nullptr) >= 1700000000; }

/**
 * Sets the clock from an HTTP Date header, such as "Wed, 19 Nov 2026 01:45:00 GMT". The receiver's network may block
 * NTP, and the Worker refuses a device list request without the right time, so its own answers set the clock.
 */
inline bool set_clock_from_http_date(const std::string &date) {
  static const char *const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char month_name[4] = {};
  int day, year, hour, minute, second;
  // A bench config, not a component, so the size of sscanf doesn't matter here
  if (sscanf(date.c_str(), "%*3s, %d %3s %d %d:%d:%d", &day, month_name, &year, &hour, &minute,  // NOLINT
             &second) != 6)
    return false;
  int month = -1;
  for (int i = 0; i != 12; i++) {
    if (strcmp(month_name, MONTHS[i]) == 0)
      month = i + 1;
  }
  if (month < 0 || year < 2024)
    return false;
  // Days from 1970-01-01 to the date (Howard Hinnant's days_from_civil)
  int y = year - (month <= 2);
  int era = y / 400;
  int yoe = y - era * 400;
  int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = int64_t(era) * 146097 + doe - 719468;
  struct timeval tv{};
  tv.tv_sec = time_t(days * 86400 + hour * 3600 + minute * 60 + second);
  return settimeofday(&tv, nullptr) == 0;
}

inline std::string devices_request_json() {
  char body[32];
  snprintf(body, sizeof(body), "{\"time\":%lld}", (long long) ::time(nullptr));
  return body;
}

/// The check's body: this build's project (the device name), ELF hash and build time, as tug_logger sends.
inline std::string firmware_check_json(int64_t build_time) {
  char elf_sha256[65];
  esp_app_get_elf_sha256(elf_sha256, sizeof(elf_sha256));
  char built[24];
  snprintf(built, sizeof(built), "%lld", (long long) build_time);
  return std::string("{\"project\":") + json_string(esp_app_get_description()->project_name) + ",\"elf_sha256\":\"" +
         elf_sha256 + "\",\"build_time\":" + built + "}";
}

}  // namespace receiver
