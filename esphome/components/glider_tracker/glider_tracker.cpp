#include "glider_tracker.h"

#include <cinttypes>
#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::glider_tracker {

ESPHOME_LOG_TAG(TAG, "glider_tracker");

static constexpr uint32_t UPDATE_MS = 1000;
// Position reports: often while flying, now and then on the ground so the receiver knows the tracker is alive
static constexpr uint32_t POSITION_FLYING_MS = 5000;
static constexpr uint32_t POSITION_GROUND_MS = 30000;
// A summary, or a takeoff report while flying, is sent this often until the clubhouse acknowledges it
static constexpr uint32_t RETRY_MS = 10000;
// After a send to the clubhouse, the radio stays on LoRa this long for the answer
static constexpr uint32_t ANSWER_HOLD_MS = 5000;
// A fix or time older than this is not used
static constexpr uint32_t MAX_FIX_AGE_MS = 5000;
// GPS dates before this are a receiver that doesn't know the date yet
static constexpr uint16_t MIN_YEAR = 2024;

// Unix time from a calendar date (days_from_civil, Howard Hinnant)
static uint32_t unix_time(int y, unsigned m, unsigned d, unsigned hh, unsigned mm, unsigned ss) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = unsigned(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = long(era) * 146097 + long(doe) - 719468;
  return uint32_t(days * 86400 + hh * 3600 + mm * 60 + ss);
}

void GliderTracker::setup() {
  this->pref_ = global_preferences->make_preference<FlightStore>(fnv1_hash("glider_tracker"));
  FlightStore store{};
  if (!this->pref_.load(&store))
    store.magic = 0;
  this->log_.load(store);
  this->save_();
  Summary flight;
  if (this->log_.get_flight(&flight)) {
    ESP_LOGI(TAG, "Flight from takeoff %" PRIu32 " still in progress from before the restart", flight.takeoff);
  }
  if (this->log_.queued() != 0) {
    ESP_LOGI(TAG, "%zu summaries waiting from before the restart", this->log_.queued());
  }
  this->want_takeoff_ = this->log_.is_flying();
  this->clubhouse_->top()->add_packet_interface_listener(
      [this](const packet_interface::PacketBuffer &data, const packet_interface::PacketMetaData &meta) {
        this->receive_(data, meta);
      });
}

void GliderTracker::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Glider tracker:\n"
                "  FLARM on the ground: %s",
                YESNO(this->flarm_on_ground_));
}

Sample GliderTracker::sample() const {
  const gps::GPSData &data = this->gps_->get_data();
  Sample sample{};
  sample.fix = this->gps_->get_age(gps::GPS_FIELD_LOCATION) < MAX_FIX_AGE_MS;
  const uint32_t time_age = this->gps_->get_age(gps::GPS_FIELD_TIME);
  if (data.has(gps::GPS_FIELD_DATE) && data.year >= MIN_YEAR && time_age < MAX_FIX_AGE_MS) {
    sample.now = unix_time(data.year, data.month, data.day, data.hour, data.minute, data.second) + time_age / 1000;
  }
  if (sample.fix) {
    sample.latitude = data.latitude();
    sample.longitude = data.longitude();
    sample.altitude_m = static_cast<int16_t>(std::lround(data.altitude()));
  }
  sample.speed_valid = this->gps_->get_age(gps::GPS_FIELD_SPEED) < MAX_FIX_AGE_MS;
  sample.speed_kmh = sample.speed_valid ? data.speed_kmh() : 0;
  return sample;
}

uint32_t GliderTracker::flight_seconds() const {
  Summary flight;
  const uint32_t now = this->sample().now;
  if (!this->log_.get_flight(&flight) || now == 0 || flight.takeoff > now)
    return 0;
  return now - flight.takeoff;
}

void GliderTracker::save_() {
  if (!this->log_.take_changed())
    return;
  this->pref_.save(&this->log_.store());
  // A summary must survive the power going off just after landing
  global_preferences->sync();
}

void GliderTracker::handle_event_(FlightLog::Event event) {
  Summary flight;
  switch (event) {
    case FlightLog::Event::TAKEOFF:
      this->log_.get_flight(&flight);
      ESP_LOGI(TAG, "Takeoff at %" PRIu32, flight.takeoff);
      this->want_takeoff_ = true;
      this->want_position_ = true;
      this->takeoff_callback_.call();
      break;
    case FlightLog::Event::LANDED:
      ESP_LOGI(TAG, "Landed; %zu summaries waiting", this->log_.queued());
      this->want_summary_ = true;
      this->want_position_ = true;
      this->landing_callback_.call();
      break;
    default:
      break;
  }
  this->save_();
}

void GliderTracker::loop() {
  const uint32_t now = millis();
  if (now - this->last_update_ms_ >= UPDATE_MS) {
    this->last_update_ms_ = now;
    this->handle_event_(this->log_.update(this->sample()));
#ifdef USE_GLIDER_TRACKER_FLARM
    // On the ground the radio stays on LoRa only while an acknowledgement may come
    if (this->flarm_ != nullptr) {
      this->flarm_->set_listening(this->log_.is_flying() || this->flarm_on_ground_ || !this->identified_ ||
                                  !this->log_.awaiting_ack());
    }
#endif
  }
  const uint32_t position_ms = this->log_.is_flying() ? POSITION_FLYING_MS : POSITION_GROUND_MS;
  if (this->want_position_ || now - this->last_position_ms_ >= position_ms)
    this->send_position_();
  if (this->want_takeoff_ || (now - this->last_takeoff_ms_ >= RETRY_MS && this->last_takeoff_ms_ != 0))
    this->send_takeoff_();
  if (this->want_summary_ || now - this->last_summary_ms_ >= RETRY_MS)
    this->send_summary_();
}

bool GliderTracker::send_(packet_interface::PacketInterface *port, const uint8_t *data, size_t length, bool answered) {
  if (!this->identified_)
    return false;
#ifdef USE_GLIDER_TRACKER_FLARM
  // The radio must be on LoRa to send; with an answer to come, it stays there for it
  if (this->flarm_ != nullptr && !this->flarm_->hold_lora(answered ? ANSWER_HOLD_MS : 0))
    return false;
#endif
  return port->top()->send_to_interface(packet_interface::PacketBuffer(data, length), {});
}

void GliderTracker::send_position_() {
  const Sample now = this->sample();
  Position pos;
  pos.flying = this->log_.is_flying();
  pos.fix = now.fix;
  pos.time = now.now;
  const gps::GPSData &data = this->gps_->get_data();
  pos.satellites = data.satellites;
#ifdef USE_SENSOR
  if (this->battery_ != nullptr && this->battery_->has_state())
    pos.battery_v = this->battery_->get_state();
#endif
  if (pos.fix) {
    pos.lat = now.latitude;
    pos.lon = now.longitude;
    pos.alt_m = now.altitude_m;
    pos.speed_kmh = now.speed_kmh;
    pos.course_deg = data.has(gps::GPS_FIELD_COURSE) ? data.course() : 0;
    const uint32_t ms = millis();
    if (this->last_altitude_ms_ != 0 && ms != this->last_altitude_ms_)
      pos.climb_cms = static_cast<int16_t>((pos.alt_m - this->last_altitude_m_) * 100000L /
                                           static_cast<long>(ms - this->last_altitude_ms_));
    this->last_altitude_m_ = pos.alt_m;
    this->last_altitude_ms_ = ms;
  }
  uint8_t buf[Position::SIZE];
  const size_t len = pos.encode(buf);
  const bool ok = this->send_(this->broadcast_, buf, len, false);
  // Tried again next loop if the radio was busy
  if (!ok)
    return;
  this->want_position_ = false;
  this->last_position_ms_ = millis();
  ESP_LOGD(TAG, "Position %" PRIu32 " sent: fix %d, %u satellites%s", ++this->position_seq_, pos.fix, pos.satellites,
           pos.flying ? LOG_STR_LITERAL(", flying") : LOG_STR_LITERAL(""));
}

void GliderTracker::send_takeoff_() {
  this->want_takeoff_ = false;
  Takeoff report;
  if (!this->log_.get_takeoff_report(&report)) {
    this->last_takeoff_ms_ = 0;
    return;
  }
  uint8_t buf[Takeoff::SIZE];
  report.encode(buf);
  const bool ok = this->send_(this->clubhouse_, buf, sizeof(buf), true);
  this->last_takeoff_ms_ = millis();
  ESP_LOGI(TAG, "Takeoff %" PRIu32 " reported: %s", report.takeoff,
           ok ? LOG_STR_LITERAL("ok") : LOG_STR_LITERAL("busy"));
}

void GliderTracker::send_summary_() {
  this->want_summary_ = false;
  this->last_summary_ms_ = millis();
  const uint8_t *slot = this->log_.next_summary();
  if (slot == nullptr)
    return;
  Summary summary;
  summary.decode(slot, SLOT);
  const bool ok = this->send_(this->clubhouse_, slot, SLOT, true);
  ESP_LOGI(TAG, "Summary for takeoff %" PRIu32 " sent: %s", summary.takeoff,
           ok ? LOG_STR_LITERAL("ok") : LOG_STR_LITERAL("busy"));
}

void GliderTracker::receive_(const packet_interface::PacketBuffer &data, const packet_interface::PacketMetaData &meta) {
  uint8_t buf[16];
  const size_t len = data.copy_to(buf, sizeof(buf));
  Ack ack;
  if (data.size() > sizeof(buf) || !ack.decode(buf, len)) {
    ESP_LOGW(TAG, "Unexpected packet of %zu bytes from the clubhouse", data.size());
    return;
  }
  const bool takeoff = ack.type == TAKEOFF_ACK;
  if (!this->log_.acknowledge(ack)) {
    ESP_LOGI(TAG, "%s acknowledgement for %" PRIu32 ", which is not waiting",
             takeoff ? LOG_STR_LITERAL("Takeoff") : LOG_STR_LITERAL("Summary"), ack.takeoff);
    return;
  }
  ESP_LOGI(TAG, "%s %" PRIu32 " acknowledged, rssi %.0f snr %.1f; %zu summaries waiting",
           takeoff ? LOG_STR_LITERAL("Takeoff") : LOG_STR_LITERAL("Summary for takeoff"), ack.takeoff, meta.rssi,
           meta.snr, this->log_.queued());
  if (takeoff) {
    this->last_takeoff_ms_ = 0;
  } else if (this->log_.next_summary() != nullptr) {
    // Send the next one soon, not after the full retry time
    this->want_summary_ = true;
  }
  this->save_();
}

void GliderTracker::fake_flight() {
  this->log_.fake_flight(this->sample(), millis() / 1000);
  ESP_LOGI(TAG, "Fake flight queued; %zu summaries waiting", this->log_.queued());
  this->want_summary_ = true;
  this->save_();
}

void GliderTracker::fake_takeoff_or_landing() {
  const FlightLog::Event event = this->log_.fake_takeoff_or_landing(this->sample(), millis() / 1000);
  ESP_LOGI(TAG, "Fake %s",
           event == FlightLog::Event::TAKEOFF ? LOG_STR_LITERAL("takeoff; another long press lands it")
                                              : LOG_STR_LITERAL("landing"));
  this->handle_event_(event);
}

}  // namespace esphome::glider_tracker
