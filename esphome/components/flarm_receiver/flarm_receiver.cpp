#include "flarm_receiver.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <strings.h>
#include <sys/time.h>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"

namespace esphome::flarm_receiver {

ESPHOME_LOG_TAG(TAG, "flarm_receiver");

static constexpr uint32_t TRAFFIC_INTERVAL_MS = 1000;
// How long a GPS sentence takes to arrive after the second it reports starts, as SoftRF assumes
static constexpr int64_t NMEA_DELAY_MS = 100;
// Before this, the time from the GPS or the system clock has not been set
static constexpr time_t MIN_VALID_TIME = 1735689600;  // 2025-01-01
static constexpr float EARTH_RADIUS_M = 6371000.0f;
static constexpr float DEG_TO_RAD = static_cast<float>(M_PI) / 180.0f;
static constexpr float KNOTS_TO_MPS = 1852.0f / 3600.0f;
static constexpr size_t MAX_FIELDS = 16;
// A slot lasts 400 ms; a packet takes about 5 ms to send
static constexpr uint32_t SLOT_MS = 400;
static constexpr uint32_t AIR_TIME_MS = 6;
// PFLAU reports transmitting for this long after a packet is sent
static constexpr uint32_t TRANSMIT_STATUS_MS = 3000;
static constexpr float CLIMB_SMOOTHING = 0.2f;
static constexpr uint32_t AUSTRALIAN_ADDRESS_BASE = 0x7C0000;
static constexpr uint8_t ADDRESS_TYPE_ICAO = 1;
static constexpr uint8_t ADDRESS_TYPE_FLARM = 2;
// Limits for two packets from one aircraft to agree
static constexpr float MAX_SPEED_MPS = 300.0f;
static constexpr float MAX_CLIMB_MPS = 50.0f;
static constexpr float POSITION_SLACK_M = 200.0f;

// FLARM's channel hash, from SoftRF's freqplan.h
static uint32_t freq_hop_hash(uint32_t value) {
  value = (value << 15) + (~value);
  value ^= value >> 12;
  value += value << 2;
  value ^= value >> 4;
  value *= 2057;
  return value ^ (value >> 16);
}

void FlarmReceiver::setup() {
  this->radio_->register_listener(this);
  if (this->transmit_ && !this->address_fixed_)
    this->set_registration("");
}

bool FlarmReceiver::australian_address(const char *registration, uint32_t &address) {
  if (strncasecmp(registration, "VH", 2) == 0) {
    registration += 2;
    if (*registration == '-')
      registration++;
  }
  if (strlen(registration) != 3)
    return false;
  uint32_t value = 0;
  for (const char *p = registration; *p != '\0'; p++) {
    // Base 36: A to Z are 0 to 25, 0 to 9 are 26 to 35
    const char c = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
    unsigned digit;
    if (c >= 'A' && c <= 'Z') {
      digit = c - 'A';
    } else if (c >= '0' && c <= '9') {
      digit = c - '0' + 26;
    } else {
      return false;
    }
    value = value * 36 + digit;
  }
  address = AUSTRALIAN_ADDRESS_BASE + value;
  return true;
}

void FlarmReceiver::set_registration(const char *registration) {
  if (!this->transmit_)
    return;
  if (this->address_fixed_) {
    if (*registration != '\0') {
      ESP_LOGD(TAG, "Registration %s ignored: the address is configured", registration);
    }
    return;
  }
  uint32_t address;
  if (australian_address(registration, address)) {
    this->own_address_ = address;
    this->own_address_type_ = ADDRESS_TYPE_ICAO;
    ESP_LOGI(TAG, "Sending as %s, ICAO address %06" PRIX32, registration, address);
    return;
  }
  // A FLARM ID made from the MAC address, which stays the same for this unit
  uint8_t mac[6];
  get_mac_address_raw(mac);
  this->own_address_ = encode_uint24(mac[3], mac[4], mac[5]);
  this->own_address_type_ = ADDRESS_TYPE_FLARM;
  if (*registration != '\0') {
    ESP_LOGW(TAG, "Registration %s is not an Australian one; sending FLARM ID %06" PRIX32, registration,
             this->own_address_);
  } else {
    ESP_LOGI(TAG, "No registration; sending FLARM ID %06" PRIX32, this->own_address_);
  }
}

void FlarmReceiver::dump_config() {
  ESP_LOGCONFIG(TAG,
                "FLARM receiver:\n"
                "  Channels: %u from %" PRIu32 " Hz, %" PRIu32 " Hz apart\n"
                "  GPS: %s\n"
                "  Traffic timeout: %" PRIu32 " ms",
                this->plan_.channels, this->plan_.base_frequency, this->plan_.spacing, YESNO(this->has_gps_()),
                this->timeout_ms_);
  if (this->transmit_) {
    ESP_LOGCONFIG(TAG, "  Transmitting as %06" PRIX32 " (%s), aircraft type %u%s", this->own_address_,
                  this->own_address_type_ == ADDRESS_TYPE_ICAO ? LOG_STR_LITERAL("ICAO") : LOG_STR_LITERAL("FLARM"),
                  this->own_aircraft_type_, this->no_track_ ? LOG_STR_LITERAL(", no tracking") : "");
  }
  if (this->has_position_ && !this->gps_fix_) {
    ESP_LOGCONFIG(TAG, "  Fixed position: %.5f, %.5f, %.0f m", this->latitude_, this->longitude_, this->altitude_);
  }
}

void FlarmReceiver::loop() {
  if (this->has_gps_())
    this->read_gps_();
  this->hop_();
  if (this->transmit_pending_)
    this->send_position_();
  const uint32_t now = App.get_loop_component_start_time();
  if (now - this->last_traffic_ms_ >= TRAFFIC_INTERVAL_MS) {
    this->last_traffic_ms_ = now;
    this->send_traffic_();
  }
}

bool FlarmReceiver::utc_ms_(int64_t &utc_ms) const {
  if (this->gps_time_valid_) {
    utc_ms = static_cast<int64_t>(millis_64()) + this->utc_offset_ms_;
    return true;
  }
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < MIN_VALID_TIME)
    return false;
  utc_ms = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
  return true;
}

void FlarmReceiver::hop_() {
  int64_t utc_ms;
  int channel = 0;
  if (this->plan_.channels > 1 && this->utc_ms_(utc_ms)) {
    // Each second has two slots, 400 to 800 ms and 800 to 1200 ms after it starts, each with its own channel.
    // Between 200 and 400 ms the radio waits on the channel of the next slot 0.
    uint32_t second = static_cast<uint32_t>(utc_ms / 1000);
    const uint32_t ms = static_cast<uint32_t>(utc_ms % 1000);
    unsigned slot = ms >= 800 || ms < 200 ? 1 : 0;
    if (ms < 200)
      second--;
    channel = this->plan_.hopping ? freq_hop_hash((second << 1) + slot) % this->plan_.channels : slot;
    // Each slot gets one packet, at a random time in it
    const uint32_t key = (second << 1) + slot;
    if (this->transmit_ && key != this->slot_key_) {
      this->slot_key_ = key;
      const int64_t start = static_cast<int64_t>(second) * 1000 + (slot ? 800 : 400);
      this->transmit_at_ms_ = start + random_uint32() % (SLOT_MS - AIR_TIME_MS * 2);
      this->transmit_pending_ = true;
    }
  }
  if (channel != this->channel_) {
    this->channel_ = channel;
    this->radio_->retune(this->plan_.base_frequency + this->plan_.spacing * channel);
  }
}

void FlarmReceiver::send_position_() {
  int64_t utc_ms;
  if (!this->utc_ms_(utc_ms) || utc_ms < this->transmit_at_ms_)
    return;
  this->transmit_pending_ = false;
  // Only with a fix: other aircraft act on this position, so it must not come from the fixed position or an old fix
  if (!this->gps_fix_ || !this->gps_time_valid_ || !this->has_altitude_)
    return;
  // Too late for the slot, after a long loop
  if (utc_ms > this->transmit_at_ms_ + SLOT_MS / 2)
    return;
  const OwnAircraft own{this->own_address_, this->own_address_type_, this->own_aircraft_type_,
                        this->no_track_,    this->latitude_,         this->longitude_,
                        this->altitude_,    this->geoid_separation_, this->ground_speed_,
                        this->course_,      this->climb_rate_,       static_cast<uint32_t>(utc_ms / 1000)};
  uint8_t packet[LEGACY_PACKET_SIZE];
  legacy_encode(own, packet);
  uint8_t frame[LEGACY_FRAME_SIZE];
  legacy_pack(packet, frame);
  if (this->radio_->transmit_packet(frame, sizeof(frame)) != sx126x::SX126xError::NONE) {
    this->status_set_warning();
    return;
  }
  this->status_clear_warning();
  this->transmit_count_++;
  this->last_transmit_ms_ = millis();
  ESP_LOGV(TAG, "Sent on channel %d", this->channel_);
}

void FlarmReceiver::read_gps_() {
  this->read_available_chunks<64>([this](const uint8_t *data, size_t len) {
    for (size_t i = 0; i != len; i++) {
      size_t line_len;
      char *line = this->line_reader_.feed(data[i], line_len);
      if (line != nullptr && line_len != 0) {
        this->parse_sentence_(line, line_len);
        this->send_line_(line, line_len);
      }
    }
  });
}

static float parse_float(const char *field, size_t len) {
  if (len == 0)
    return NAN;
  return strtof(field, nullptr);
}

static unsigned two_digits(const char *s) { return (s[0] - '0') * 10 + (s[1] - '0'); }

// ddmm.mmmm or dddmm.mmmm with its hemisphere letter
static float parse_coordinate(const char *field, size_t len, char hemisphere) {
  const float value = parse_float(field, len);
  if (std::isnan(value))
    return NAN;
  const float degrees = std::floor(value / 100.0f);
  const float result = degrees + (value - degrees * 100.0f) / 60.0f;
  return hemisphere == 'S' || hemisphere == 'W' ? -result : result;
}

void FlarmReceiver::parse_sentence_(char *line, size_t len) {
  if (len < 10 || line[0] != '$')
    return;
  const char *star = static_cast<const char *>(memchr(line, '*', len));
  if (star == nullptr || star + 3 > line + len)
    return;
  uint8_t sum = 0;
  for (const char *p = line + 1; p != star; p++)
    sum ^= static_cast<uint8_t>(*p);
  if (sum != strtoul(star + 1, nullptr, 16))
    return;

  // fields[0] is the sentence name; each field ends at the next comma or the star
  const char *fields[MAX_FIELDS];
  size_t lengths[MAX_FIELDS];
  size_t count = 0;
  const char *start = line + 1;
  for (const char *p = start; count != MAX_FIELDS; p++) {
    if (*p == ',' || p == star) {
      fields[count] = start;
      lengths[count++] = p - start;
      start = p + 1;
      if (p == star)
        break;
    }
  }
  if (lengths[0] != 5)
    return;
  const char *type = fields[0] + 2;

  if (memcmp(type, "RMC", 3) == 0 && count >= 10) {
    // time, status, latitude, N/S, longitude, E/W, speed (knots), course, date
    this->gps_fix_ = lengths[2] == 1 && fields[2][0] == 'A';
    // Without a fix the time comes from the receiver's own clock, which can be a quarter of a second out
    if (!this->gps_fix_) {
      this->gps_time_valid_ = false;
    } else if (lengths[1] >= 6 && lengths[9] == 6) {
      ESPTime t{};
      t.hour = two_digits(fields[1]);
      t.minute = two_digits(fields[1] + 2);
      t.second = two_digits(fields[1] + 4);
      t.day_of_month = two_digits(fields[9]);
      t.month = two_digits(fields[9] + 2);
      t.year = 2000 + two_digits(fields[9] + 4);
      t.recalc_timestamp_utc(false);
      if (t.timestamp >= MIN_VALID_TIME) {
        const int64_t fraction_ms = lengths[1] > 7 ? std::lround(parse_float(fields[1] + 6, lengths[1] - 6) * 1000) : 0;
        const int64_t utc_ms = static_cast<int64_t>(t.timestamp) * 1000 + fraction_ms;
        const int64_t offset = utc_ms - (static_cast<int64_t>(millis_64()) - NMEA_DELAY_MS);
        // A late sentence makes the offset too small: keep the largest, but let it fall slowly so that it follows
        // the drift of the local clock
        if (!this->gps_time_valid_ || std::llabs(offset - this->utc_offset_ms_) > 1000) {
          this->utc_offset_ms_ = offset;
        } else {
          this->utc_offset_ms_ = std::max(offset, this->utc_offset_ms_ - 1);
        }
        this->gps_time_valid_ = true;
      }
    }
    if (this->gps_fix_) {
      const float latitude = parse_coordinate(fields[3], lengths[3], lengths[4] ? fields[4][0] : 'N');
      const float longitude = parse_coordinate(fields[5], lengths[5], lengths[6] ? fields[6][0] : 'E');
      if (!std::isnan(latitude) && !std::isnan(longitude)) {
        this->latitude_ = latitude;
        this->longitude_ = longitude;
        this->has_position_ = true;
      }
      const float speed = parse_float(fields[7], lengths[7]);
      this->ground_speed_ = std::isnan(speed) ? 0 : speed * KNOTS_TO_MPS;
      const float course = parse_float(fields[8], lengths[8]);
      if (!std::isnan(course))
        this->course_ = course;
    }
  } else if (memcmp(type, "GGA", 3) == 0 && count >= 12) {
    // time, latitude, N/S, longitude, E/W, quality, satellites, HDOP, altitude, M, geoid separation, M
    if (lengths[6] == 0 || fields[6][0] == '0') {
      this->has_altitude_ = false;
    } else {
      const float altitude = parse_float(fields[9], lengths[9]);
      const float separation = parse_float(fields[11], lengths[11]);
      if (!std::isnan(altitude)) {
        const uint32_t now = millis();
        const float seconds = (now - this->last_altitude_ms_) / 1000.0f;
        if (this->has_altitude_ && seconds > 0.1f && seconds < 5.0f) {
          // Smoothed, as the GPS altitude jumps by a meter or so from fix to fix
          this->climb_rate_ += CLIMB_SMOOTHING * ((altitude - this->altitude_) / seconds - this->climb_rate_);
        } else {
          this->climb_rate_ = 0;
        }
        this->altitude_ = altitude;
        this->last_altitude_ms_ = now;
        this->has_altitude_ = true;
      }
      if (!std::isnan(separation))
        this->geoid_separation_ = separation;
    }
  }
}

void FlarmReceiver::on_packet(const std::vector<uint8_t> &frame, float rssi, float snr) {
  uint8_t packet[LEGACY_PACKET_SIZE];
  if (!legacy_unpack(frame.data(), frame.size(), packet)) {
    ESP_LOGVV(TAG, "Bad frame on channel %d, RSSI %.1f dBm", this->channel_, rssi);
    return;
  }
  int64_t utc_ms;
  if (!this->utc_ms_(utc_ms) || !this->has_position_) {
    ESP_LOGD(TAG, "Packet from %06" PRIX32 " ignored: no %s yet", encode_uint24(packet[2], packet[1], packet[0]),
             this->has_position_ ? LOG_STR_LITERAL("time") : LOG_STR_LITERAL("position"));
    return;
  }
  const OwnPosition own{this->latitude_, this->longitude_, this->geoid_separation_,
                        static_cast<uint32_t>(utc_ms / 1000)};
  LegacyTarget target{};
  if (!legacy_decode(packet, own, target)) {
    ESP_LOGD(TAG, "Packet from %06" PRIX32 " not decoded", target.address);
    return;
  }
  if (this->transmit_ && target.address == this->own_address_)
    return;
  this->packet_count_++;

  const uint32_t now = millis();
  Traffic *slot = nullptr;
  for (auto &entry : this->traffic_) {
    if (entry.active && entry.target.address == target.address) {
      slot = &entry;
      break;
    }
    if (slot == nullptr || (slot->active && (!entry.active || entry.last_seen_ms < slot->last_seen_ms)))
      slot = &entry;
  }
  if (slot->active && slot->target.address == target.address) {
    // A packet decrypted with the wrong key passes the checks now and then, and gives a random place: an aircraft is
    // only reported once two packets agree, and one far from the last place is dropped.
    const float seconds = (now - slot->last_seen_ms) / 1000.0f;
    const float north = (target.latitude - slot->target.latitude) * DEG_TO_RAD * EARTH_RADIUS_M;
    const float east = (target.longitude - slot->target.longitude) * DEG_TO_RAD * EARTH_RADIUS_M *
                       std::cos(target.latitude * DEG_TO_RAD);
    const bool agrees = std::sqrt(north * north + east * east) < MAX_SPEED_MPS * seconds + POSITION_SLACK_M &&
                        std::fabs(target.altitude - slot->target.altitude) < MAX_CLIMB_MPS * seconds + POSITION_SLACK_M;
    if (!agrees) {
      ESP_LOGD(TAG, "%06" PRIX32 ": a packet far from the last position was dropped", target.address);
      if (slot->confirmed)
        return;
    } else if (!slot->confirmed) {
      slot->confirmed = true;
      ESP_LOGI(TAG, "New aircraft %06" PRIX32 " (type %u), RSSI %.1f dBm", target.address, target.aircraft_type, rssi);
    }
  } else {
    slot->active = true;
    slot->confirmed = false;
  }
  slot->target = target;
  slot->last_seen_ms = now;
  slot->rssi = rssi;
  ESP_LOGD(TAG, "%06" PRIX32 ": %.5f, %.5f, %.0f m, %.1f m/s, %.0f deg, climb %.1f m/s, channel %d, RSSI %.1f dBm",
           target.address, target.latitude, target.longitude, target.altitude, target.speed, target.course,
           target.climb_rate, this->channel_, rssi);
}

void FlarmReceiver::send_traffic_() {
  const uint32_t now = millis();
  unsigned count = 0;
  for (auto &entry : this->traffic_) {
    if (entry.active && now - entry.last_seen_ms > this->timeout_ms_) {
      if (entry.confirmed) {
        ESP_LOGI(TAG, "Aircraft %06" PRIX32 " lost", entry.target.address);
      }
      entry.active = false;
    }
    if (entry.active && entry.confirmed)
      count++;
  }
  if (this->nmea_callback_.size() == 0)
    return;

  char body[NMEA_LINE_SIZE];
  const unsigned gps = this->gps_fix_ ? (this->ground_speed_ > 5.0f ? 2 : 1) : 0;
  const unsigned tx = this->transmit_count_ != 0 && now - this->last_transmit_ms_ < TRANSMIT_STATUS_MS ? 1 : 0;
  snprintf(body, sizeof(body), "PFLAU,%u,%u,%u,1,0,,0,,,", count, tx, gps);
  this->send_sentence_(body);
  if (!this->has_position_)
    return;

  const float cos_lat = std::cos(this->latitude_ * DEG_TO_RAD);
  for (const auto &entry : this->traffic_) {
    if (!entry.active || !entry.confirmed)
      continue;
    const LegacyTarget &t = entry.target;
    const long north = std::lround((t.latitude - this->latitude_) * DEG_TO_RAD * EARTH_RADIUS_M);
    const long east = std::lround((t.longitude - this->longitude_) * DEG_TO_RAD * EARTH_RADIUS_M * cos_lat);
    const long vertical = std::lround(t.altitude - this->altitude_);
    const unsigned id_type = t.address_type <= 2 ? t.address_type : 0;
    char turn[12] = "";
    if (!std::isnan(t.turn_rate))
      snprintf(turn, sizeof(turn), "%.0f", t.turn_rate);
    snprintf(body, sizeof(body), "PFLAA,0,%ld,%ld,%ld,%u,%06" PRIX32 ",%.0f,%s,%.0f,%.1f,%X", north, east, vertical,
             id_type, t.address, t.course, turn, t.speed, t.climb_rate, t.aircraft_type);
    this->send_sentence_(body);
  }
}

void FlarmReceiver::send_sentence_(const char *body) {
  uint8_t sum = 0;
  for (const char *p = body; *p != '\0'; p++)
    sum ^= static_cast<uint8_t>(*p);
  char line[NMEA_LINE_SIZE + 8];
  const int len = snprintf(line, sizeof(line), "$%s*%02X", body, sum);
  if (len > 0 && static_cast<size_t>(len) < sizeof(line))
    this->send_line_(line, len);
}

void FlarmReceiver::send_line_(const char *line, size_t len) {
  if (this->nmea_callback_.size() == 0)
    return;
  char buf[NMEA_LINE_SIZE + 10];
  if (len + 2 > sizeof(buf))
    return;
  memcpy(buf, line, len);
  buf[len] = '\r';
  buf[len + 1] = '\n';
  this->nmea_callback_.call(reinterpret_cast<const uint8_t *>(buf), len + 2);
}

}  // namespace esphome::flarm_receiver
