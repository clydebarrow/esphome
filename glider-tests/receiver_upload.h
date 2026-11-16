#pragma once
// Upload of flight summaries from the clubhouse receiver to the flight-upload Worker (POST /api/flights).
//
// The receiver keeps no queue: each glider keeps its summary in flash and sends it every 10 s until acknowledged, so
// the receiver posts each summary it hears and acknowledges it only once the Worker has answered. One upload runs at
// a time; a summary heard meanwhile is ignored, and comes again.
//
// Positions are collected as they are heard and posted in batches to /api/positions for the live map. A batch that
// fails is dropped: a position is soon out of date anyway.

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "mbedtls/md.h"

#include "glider_packets.h"

namespace receiver {

/// A glider the receiver has a stack for. ack sends the acknowledgement for a takeoff time on that stack.
struct Glider {
  uint16_t address;
  const char *name;
  const char *registration;
  const char *type;
  std::function<bool(uint32_t)> ack;
};

inline std::vector<Glider> gliders;

/// The upload in progress
struct Upload {
  bool busy{false};
  size_t glider{0};
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

inline const Glider *find_glider(uint16_t address) {
  for (const auto &glider : gliders) {
    if (glider.address == address)
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
inline std::string position_json(const Glider &glider, const glider::Position &pos, float rssi, float snr) {
  char buf[320];
  int n = snprintf(buf, sizeof(buf), "{\"registration\":%s,\"time\":%u,\"fix\":%s,\"flying\":%s,\"satellites\":%u",
                   json_string(glider.registration).c_str(), (unsigned) pos.time, pos.fix ? "true" : "false",
                   pos.flying ? "true" : "false", (unsigned) pos.satellites);
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

}  // namespace receiver
