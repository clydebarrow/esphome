#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::skyecho_wifi {

static constexpr size_t SSID_SIZE = 33;
static constexpr size_t MAX_SKIPPED = 4;

inline void copy_ssid(char *dest, const char *src) {
  strncpy(dest, src, SSID_SIZE - 1);
  dest[SSID_SIZE - 1] = 0;
}

/// SkyEcho access points are named "SkyEcho_" and part of the serial number
inline bool is_skyecho_ssid(const char *ssid) { return strncmp(ssid, "SkyEcho", 7) == 0; }

/// A network seen in a scan.
struct Candidate {
  const char *ssid;
  int8_t rssi;
  bool secured;
};

/**
 * Which SkyEcho to join, and which to leave alone for a while. The one remembered, the glider's own, is chosen while
 * its signal is usable; otherwise the strongest open SkyEcho in range, as in a glider its own is a metre away and the
 * others are well off. One that is skipped, as it sent no traffic or its signal faded, is not chosen until its time
 * is up.
 */
class SkyEchoChooser {
 public:
  explicit SkyEchoChooser(int8_t weak_rssi) : weak_rssi_(weak_rssi) {}

  void set_remembered(const char *ssid) { copy_ssid(this->remembered_, ssid); }
  const char *get_remembered() const { return this->remembered_; }

  /// Leaves a SkyEcho alone until the time until, in ms.
  void skip(const char *ssid, uint32_t until) {
    Skipped *slot = &this->skipped_[0];
    for (auto &skipped : this->skipped_) {
      if (strcmp(skipped.ssid, ssid) == 0 || skipped.ssid[0] == 0) {
        slot = &skipped;
        break;
      }
      if (skipped.until < slot->until)
        slot = &skipped;
    }
    copy_ssid(slot->ssid, ssid);
    slot->until = until;
  }
  bool is_skipped(const char *ssid, uint32_t now) const {
    for (const auto &skipped : this->skipped_) {
      if (skipped.ssid[0] != 0 && strcmp(skipped.ssid, ssid) == 0 && static_cast<int32_t>(skipped.until - now) > 0)
        return true;
    }
    return false;
  }

  /// The SkyEcho to join from a scan at time now, in ms; nullptr if none will do.
  const char *choose(const Candidate *candidates, size_t count, uint32_t now) const {
    const Candidate *best = nullptr;
    for (size_t i = 0; i != count; i++) {
      const Candidate &c = candidates[i];
      if (c.secured || !is_skyecho_ssid(c.ssid) || this->is_skipped(c.ssid, now))
        continue;
      if (this->remembered_[0] != 0 && strcmp(c.ssid, this->remembered_) == 0 && c.rssi >= this->weak_rssi_)
        return c.ssid;
      if (best == nullptr || c.rssi > best->rssi)
        best = &c;
    }
    return best == nullptr ? nullptr : best->ssid;
  }

  int8_t get_weak_rssi() const { return this->weak_rssi_; }

 protected:
  struct Skipped {
    char ssid[SSID_SIZE];
    uint32_t until;
  };

  int8_t weak_rssi_;
  char remembered_[SSID_SIZE]{};
  Skipped skipped_[MAX_SKIPPED]{};
};

}  // namespace esphome::skyecho_wifi
