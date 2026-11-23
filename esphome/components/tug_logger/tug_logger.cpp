#include "esphome/core/defines.h"

#if defined(USE_ESP32) || defined(USE_HOST)

#include "tug_logger.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <sys/stat.h>

#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"

#include "tow_uploader.h"

#ifdef USE_TUG_LOGGER_UPLOAD_LINK
#include "esphome/components/packet_interface/basic_addressing.h"
#include "esphome/components/packet_interface/xxtea_encryption.h"
#endif

namespace esphome::tug_logger {

ESPHOME_LOG_TAG(TAG, "tug_logger");

static constexpr uint32_t TICK_MS = 1000;
static constexpr uint32_t STORE_RETRY_MS = 5000;
static constexpr uint32_t FLIGHT_SAVE_MS = 30000;
static constexpr uint32_t CARD_CHECK_MS = 5000;
static constexpr uint32_t NETWORK_CHECK_MS = 5000;
// How long a flight found at start waits for a position before it is closed
static constexpr uint32_t RESUME_WAIT_MS = 120000;
static constexpr uint32_t NMEA_MAX_AGE_MS = 5000;
// A barometer reading older than this is not used
static constexpr uint32_t BARO_MAX_AGE_MS = 5000;
static constexpr uint32_t POSITION_LOG_MS = 15000;
// Position reports for the live map, as often as a glider tracker sends them
static constexpr uint32_t POSITION_FLYING_MS = 5000;
static constexpr uint32_t POSITION_GROUND_MS = 30000;
// The flight record: how much of the time on the ground goes before a takeoff, and how often it is written
static constexpr size_t GROUND_LINES = 60;
static constexpr uint32_t FLIGHT_LOG_FLUSH_MS = 10000;

void TugLogger::setup() {
  this->detector_ = FlightDetector(this->settings_);
#ifdef USE_TUG_LOGGER_UPLOAD_LINK
  if (this->tow_link_ != nullptr)
    this->tow_link_->setup();
#endif
  this->last_activity_ms_ = millis();
}

void TugLogger::set_pressure_sensor(sensor::Sensor *sensor) {
  this->pressure_sensor_ = sensor;
  sensor->add_on_state_callback([this](float pressure_hpa) {
    // A failed reading is NaN; anything outside this range is not a cabin in flight or on the ground
    if (!std::isfinite(pressure_hpa) || pressure_hpa < 300.0f || pressure_hpa > 1100.0f)
      return;
    this->baro_m_ = pressure_altitude_m(pressure_hpa);
    this->baro_ms_ = millis();
  });
}

void TugLogger::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Tug logger:\n"
                "  Takeoff: %.1f m/s for %" PRIu32 " ms\n"
                "  Landing: %.1f m/s for %" PRIu32 " ms\n"
                "  Shutdown window: %" PRIu32 " ms",
                this->settings_.takeoff_speed_mps, this->settings_.takeoff_confirm_ms,
                this->settings_.landing_speed_mps, this->settings_.landing_confirm_ms, this->shutdown_window_ms_);
}

std::string local_time_text(time_t epoch) { return ESPTime::from_epoch_local(epoch).strftime("%Y-%m-%d %H:%M"); }

void TugLogger::on_safe_shutdown() {
  if (this->store_ready_ && this->detector_.is_flying())
    this->save_flight_();
  this->flush_flight_log_();
}

std::string TugLogger::log_line_(const PositionSample &sample) const {
  char line[192];
  int length = snprintf(line, sizeof(line), "%.1f,%lld,%u,", sample.now_ms / 1000.0f, (long long) sample.epoch,
                        unsigned(this->source_));
  auto add = [&](bool valid, float value) {
    length += valid ? snprintf(line + length, sizeof(line) - length, "%.0f,", value)
                    : snprintf(line + length, sizeof(line) - length, ",");
  };
  add(sample.speed_valid, sample.ground_speed_mps / KNOTS_TO_MPS);
  add(sample.pressure_valid, sample.pressure_m / METERS_PER_FOOT);
  add(sample.geometric_valid, sample.geometric_m / METERS_PER_FOOT);
  const bool flying = this->detector_.is_flying();
  add(flying, float(this->detector_.current_height_ft()));
  add(flying, float(this->detector_.flight().max_height_ft));
  const SkyEchoState &skyecho = this->skyecho_state_;
  add(skyecho.present, skyecho.gps_valid ? 1.0f : 0.0f);
  if (skyecho.has_ownship) {
    length += snprintf(line + length, sizeof(line) - length, "%.1f,", skyecho.ownship_age_ms / 1000.0f);
  } else {
    length += snprintf(line + length, sizeof(line) - length, ",");
  }
  add(skyecho.geometric_valid, skyecho.geometric_m / METERS_PER_FOOT);
  // Where the tug was, from the same GPS as the speed, so a tow can be drawn on a map beside the glider's track
  const Location &location = this->location_;
  if (location.valid) {
    length += snprintf(line + length, sizeof(line) - length, "%.6f,%.6f,", location.lat, location.lon);
  } else {
    length += snprintf(line + length, sizeof(line) - length, ",,");
  }
  add(location.valid && location.course_valid, location.course_deg);
  // Both barometers, whichever the height is from
  const PressureReadings &pressure = this->pressure_readings_;
  add(pressure.baro_valid, pressure.baro_m / METERS_PER_FOOT);
  add(pressure.skyecho_valid, pressure.skyecho_m / METERS_PER_FOOT);
  // The last comma ends the line instead
  line[length - 1] = '\n';
  return line;
}

const char *TugLogger::pressure_source_text_(const FlightSnapshot &flight) {
  if (flight.kind != AltitudeKind::ALTITUDE_KIND_PRESSURE)
    return "not used";
  switch (PressureSource(flight.pressure_source)) {
    case PressureSource::PRESSURE_SOURCE_BARO:
      return "the display's barometer";
    case PressureSource::PRESSURE_SOURCE_SKYECHO:
      return "the SkyEcho";
    default:
      return "either";
  }
}

void TugLogger::start_flight_log_(const char *why) {
  const FlightSnapshot &flight = this->detector_.flight();
  if (!this->store_ready_ || !this->store_.start_flight_log(flight.launch, millis())) {
    ESP_LOGW(TAG, "The flight record could not be started");
    return;
  }
  this->flight_logging_ = true;
  const char *kind = flight.kind == AltitudeKind::ALTITUDE_KIND_PRESSURE    ? "pressure"
                     : flight.kind == AltitudeKind::ALTITUDE_KIND_GEOMETRIC ? "GPS"
                                                                            : "none yet";
  char field[96];
  if (!this->launch_field_name_.empty()) {
    snprintf(field, sizeof(field), "%s", this->launch_field_name_.c_str());
  } else {
    snprintf(field, sizeof(field), "%s",
             this->provisioning_.airfields.empty() ? "home field" : "no airfield within 3 km, from GPS altitude");
  }
  char header[1024];
  snprintf(
      header, sizeof(header),
      "# %s. Heights from %s altitude, reference %s%.0f ft%s; field elevation %.0f ft, %s\n"
      "# source: 0 none, 1 SkyEcho, 2 NMEA. Speed in knots, altitudes and heights in feet; heights above the field\n"
      "# skyecho_gps: 1 if its heartbeat said its GPS had a position; ownship_age_s: since its last ownship report; "
      "skyecho_gps_alt_ft: its own GPS altitude. lat and lon in degrees, course_deg true, from the source's GPS\n"
      "# pressure_alt_ft: the one the height is from (%s); baro_alt_ft: the display's barometer; skyecho_pressure_ft: "
      "the SkyEcho's\n"
      "uptime_s,utc,source,speed_kt,pressure_alt_ft,gps_alt_ft,height_ft,max_height_ft,skyecho_gps,ownship_age_s,"
      "skyecho_gps_alt_ft,lat,lon,course_deg,baro_alt_ft,skyecho_pressure_ft\n",
      why, kind, flight.has_reference ? "" : "(none, the field elevation) ",
      flight.has_reference ? flight.reference_m / METERS_PER_FOOT : flight.field_elevation_ft,
      flight.approximate ? ", approximate" : "", flight.field_elevation_ft, field, this->pressure_source_text_(flight));
  this->flight_log_ = header;
  for (const std::string &line : this->ground_lines_)
    this->flight_log_ += line;
  this->ground_lines_.clear();
  this->flush_flight_log_();
}

void TugLogger::flush_flight_log_() {
  this->last_log_flush_ms_ = millis();
  if (!this->flight_logging_ || this->flight_log_.empty())
    return;
  if (!this->store_.append_flight_log(this->flight_log_)) {
    ESP_LOGW(TAG, "The flight record could not be written");
  }
  this->flight_log_.clear();
}

void TugLogger::log_sample_(const PositionSample &sample, FlightEvent event) {
  std::string line = this->log_line_(sample);
  if (event == FlightEvent::FLIGHT_EVENT_TAKEOFF)
    this->start_flight_log_("Takeoff");
  if (this->detector_.is_flying() || event == FlightEvent::FLIGHT_EVENT_LANDED) {
    this->flight_log_ += line;
    if (event == FlightEvent::FLIGHT_EVENT_LANDED) {
      const FlightSnapshot &flight = this->detector_.flight();
      char summary[128];
      snprintf(summary, sizeof(summary),
               "# Landed: %" PRIu32 " s, highest %" PRId32 " ft at %" PRIu32 " s, altitude %" PRId32 " ft MSL%s\n",
               flight.elapsed_s, flight.max_height_ft, flight.climb_s,
               this->detector_.to_altitude_ft(flight.max_height_ft), flight.approximate ? ", approximate" : "");
      this->flight_log_ += summary;
      this->flush_flight_log_();
      this->flight_logging_ = false;
      this->store_.end_flight_log();
    } else if (millis() - this->last_log_flush_ms_ >= FLIGHT_LOG_FLUSH_MS) {
      this->flush_flight_log_();
    }
    return;
  }
  this->ground_lines_.push_back(std::move(line));
  if (this->ground_lines_.size() > GROUND_LINES)
    this->ground_lines_.pop_front();
}

void TugLogger::loop() {
#ifdef USE_TUG_LOGGER_UPLOAD_LINK
  if (this->tow_link_ != nullptr)
    this->tow_link_->loop();
#endif
  const uint32_t now = millis();
  if (now - this->last_tick_ms_ < TICK_MS)
    return;
  this->last_tick_ms_ = now;
  this->tick_();
}

time_t TugLogger::epoch_() {
  if (this->time_ == nullptr)
    return 0;
  const ESPTime now = this->time_->utcnow();
  return now.is_valid() ? now.timestamp : 0;
}

std::string TugLogger::clock_text() {
  if (!this->clock_valid())
    return "--:--";
  return this->time_->now().strftime("%H:%M");
}

size_t TugLogger::review_count() { return this->store_.review_list().size(); }

static uint32_t day_of(const ESPTime &time) {
  return time.is_valid() ? time.year * 10000u + time.month * 100u + time.day_of_month : 0;
}

uint32_t TugLogger::today_() { return this->time_ != nullptr ? day_of(this->time_->now()) : 0; }

void TugLogger::record_flight_(uint32_t seconds) {
  this->totals_.roll_to(this->today_());
  this->totals_.add_flight(seconds);
  this->store_.save_totals(this->totals_);
}

void TugLogger::discard(const TowRecord &record) {
  this->store_.discard(record.id);
  // A launch from an earlier day has gone from the counts already
  if (record.launch == 0 || day_of(ESPTime::from_epoch_local(record.launch)) == this->totals_.day) {
    this->totals_.remove_flight(record.duration_s);
    this->store_.save_totals(this->totals_);
  }
}

void TugLogger::resend_failed_tows() {
  if (!this->store_ready_)
    return;
  const size_t count = this->store_.resend_failed();
  ESP_LOGI(TAG, "%zu refused tows queued again", count);
  this->failed_ = this->store_.failed_count();
  this->queued_ = this->store_.queued_count();
  this->last_upload_ms_ = 0;
  wake_tow_uploader();
}

void TugLogger::mark_refuelled() {
  this->totals_.fuel_s = 0;
  this->store_.save_totals(this->totals_);
}

void TugLogger::next_details_changed() {
  if (!this->next_details_.tug_pilot.empty())
    this->store_.remember(this->next_details_);
}

ConfirmResult TugLogger::confirm(uint32_t id) {
  const ConfirmResult result = this->store_.confirm(id, this->provisioning_.tug_registration);
  if (result == ConfirmResult::CONFIRM_RESULT_OK)
    this->queued_ = this->store_.queued_count();
  return result;
}

#ifdef USE_TUG_LOGGER_UPLOAD_LINK
// The LoRa address and key are the tug's own, from the dashboard, so they come from the tug file rather than the build
void TugLogger::apply_lora_provisioning_() {
  if (this->lora_addressing_ == nullptr || this->lora_encryption_ == nullptr)
    return;
  uint8_t key[32];
  const auto &p = this->provisioning_;
  if (!this->provisioned_ || p.lora_address == 0 || p.lora_address == 0xFFFF ||
      !parse_hex(p.lora_key, key, sizeof(key))) {
    ESP_LOGW(TAG, "The tug file has no LoRa address and key, so tows don't go over LoRa; make a new one in the "
                  "dashboard");
    return;
  }
  this->lora_addressing_->set_address(p.lora_address);
  this->lora_encryption_->set_key(key);
  ESP_LOGI(TAG, "LoRa address 0x%04X", p.lora_address);
}
#endif

bool TugLogger::start_store_() {
  if (!this->store_.begin())
    return false;
  this->card_ok_ = true;
  this->provisioned_ = this->store_.load_provisioning(this->provisioning_);
  if (this->provisioned_) {
    // The list last fetched from the Worker is newer than the tug file's
    if (this->store_.load_airfields(this->provisioning_)) {
      ESP_LOGI(TAG, "%zu airfields from the Worker", this->provisioning_.airfields.size());
    }
    this->detector_.set_field_elevation_ft(this->provisioning_.field_elevation_ft);
    this->store_.set_default_field_elevation_ft(this->provisioning_.field_elevation_ft);
    ESP_LOGI(TAG, "Tug %s, field elevation %.0f ft", this->provisioning_.tug_registration.c_str(),
             this->provisioning_.field_elevation_ft);
  } else {
    ESP_LOGW(TAG, "No usable tug.json or tug-*.json in the root of the SD card");
  }
#ifdef USE_TUG_LOGGER_UPLOAD_LINK
  this->apply_lora_provisioning_();
#endif
  if (this->upload_url_ != nullptr)
    this->provisioning_.upload_url = this->upload_url_;
  if (this->upload_key_ != nullptr)
    this->provisioning_.upload_key = this->upload_key_;
  this->queued_ = this->store_.queued_count();
  this->failed_ = this->store_.failed_count();
  this->totals_ = this->store_.load_totals();
  this->has_saved_flight_ = this->store_.load_flight(this->saved_flight_, this->saved_details_, this->saved_at_);
  if (this->has_saved_flight_) {
    ESP_LOGI(TAG, "A flight was in progress when the device stopped");
  }
  const auto recent = this->store_.recent_pilots();
  if (this->next_details_.tug_pilot.empty() && !recent.empty())
    this->next_details_.tug_pilot = recent.front();
  if (this->provisioned_) {
    if (!start_tow_uploader(this)) {
      ESP_LOGE(TAG, "The upload task could not start");
    }
  }
  return true;
}

// The flight ended at about the time it was last saved. Its figures may be short, so it is marked approximate
void TugLogger::close_saved_flight_() {
  TowRecord record;
  record.launch = this->saved_flight_.launch;
  record.duration_s = this->saved_flight_.elapsed_s;
  record.climb_s = this->saved_flight_.climb_s;
  record.max_height_ft = this->saved_flight_.max_height_ft;
  record.field_elevation_ft = this->saved_flight_.field_elevation_ft;
  record.approximate = true;
  record.details = this->saved_details_;
  this->record_flight_(record.duration_s);
  this->store_.add_review(std::move(record));
  this->store_.clear_flight();
  this->has_saved_flight_ = false;
  this->landed_callback_.call();
}

PositionSample TugLogger::read_position_() {
  PositionSample sample;
  sample.now_ms = millis();
  sample.epoch = this->epoch_();
  this->source_ = GpsSource::GPS_SOURCE_NONE;
  this->location_ = Location{};
  PressureReadings &pressure = this->pressure_readings_;
  pressure = PressureReadings{};
  pressure.baro_valid = this->baro_ms_ != 0 && sample.now_ms - this->baro_ms_ < BARO_MAX_AGE_MS;
  pressure.baro_m = this->baro_m_;
  // The NMEA fix (the relay's own GNSS) comes first while it is fresh: a dedicated receiver with a clear view gives a
  // better position and GPS altitude than the SkyEcho in the cabin, which stands in when there is none
  const bool nmea = this->nmea_ms_ != 0 && sample.now_ms - this->nmea_ms_ < NMEA_MAX_AGE_MS;
  if (nmea) {
    this->source_ = GpsSource::GPS_SOURCE_NMEA;
    sample.speed_valid = this->nmea_speed_valid_;
    sample.ground_speed_mps = this->nmea_speed_mps_;
    sample.geometric_valid = this->nmea_altitude_valid_;
    sample.geometric_source = uint8_t(GpsSource::GPS_SOURCE_NMEA);
    sample.geometric_m = this->nmea_altitude_m_;
    this->location_.valid = this->nmea_lat_ != 0.0 || this->nmea_lon_ != 0.0;
    this->location_.lat = this->nmea_lat_;
    this->location_.lon = this->nmea_lon_;
    this->location_.course_valid = this->nmea_course_valid_;
    this->location_.course_deg = this->nmea_course_deg_;
    this->location_.satellites = this->nmea_satellites_;
  }
#ifdef USE_TUG_LOGGER_SKYECHO
  this->skyecho_state_ = SkyEchoState{};
  if (this->skyecho_ != nullptr) {
    SkyEchoState &state = this->skyecho_state_;
    state.present = true;
    state.gps_valid = this->skyecho_->heartbeat_gps_valid();
    state.has_ownship = this->skyecho_->get_ownship_age_ms(&state.ownship_age_ms);
    state.geometric_valid = this->skyecho_->get_ownship_geo_altitude(&state.geometric_m);
    // Its pressure altitude is kept either way: the flight log records it beside the GPS altitude. Its barometer
    // needs no GPS fix, and it loses its fix in the cabin for minutes on end
    pressure.skyecho_valid = this->skyecho_->get_pressure_altitude(&pressure.skyecho_m);
    skyecho::OwnshipT ownship;
    if (this->skyecho_->get_ownship_position(&ownship)) {
      const auto &report = ownship.report;
      if (!nmea) {
        this->source_ = GpsSource::GPS_SOURCE_SKYECHO;
        sample.speed_valid = report.isGroundSpeedValid;
        sample.ground_speed_mps = report.groundSpeed;
        sample.geometric_valid = this->skyecho_->get_ownship_geo_altitude(&sample.geometric_m);
        sample.geometric_source = uint8_t(GpsSource::GPS_SOURCE_SKYECHO);
        // A report with no position has zero latitude and longitude
        this->location_.valid = report.latitude != 0.0f || report.longitude != 0.0f;
        this->location_.lat = report.latitude;
        this->location_.lon = report.longitude;
        this->location_.course_valid = report.isTrackValid;
        this->location_.course_deg = report.track;
        this->skyecho_->get_gps_satellites(&this->location_.satellites);
      }
      // Only the SkyEcho reports a vertical speed
      this->location_.climb_mps = report.isVerticalSpeedValid ? report.verticalSpeed : 0.0f;
    }
  }
#endif
  // The display's own barometer first: it never drops out with the SkyEcho's Wi-Fi. A flight keeps the barometer it
  // took off with, as the two read tens of feet apart; the detector carries a gap in it on with GPS.
  uint8_t source = 0;
  const FlightSnapshot &flight = this->detector_.flight();
  if (this->detector_.is_flying() && flight.kind == AltitudeKind::ALTITUDE_KIND_PRESSURE)
    source = flight.pressure_source;
  const bool use_baro = source == uint8_t(PressureSource::PRESSURE_SOURCE_BARO) || (source == 0 && pressure.baro_valid);
  if (use_baro) {
    sample.pressure_valid = pressure.baro_valid;
    sample.pressure_m = pressure.baro_m;
    sample.pressure_source = uint8_t(PressureSource::PRESSURE_SOURCE_BARO);
  } else {
    sample.pressure_valid = pressure.skyecho_valid;
    sample.pressure_m = pressure.skyecho_m;
    sample.pressure_source = uint8_t(PressureSource::PRESSURE_SOURCE_SKYECHO);
  }
  return sample;
}

#ifdef USE_TUG_LOGGER_GPS
void TugLogger::on_update(TinyGPSPlus &tiny_gps) {
  if (!tiny_gps.location.isValid())
    return;
  this->set_backup_fix(tiny_gps.speed.isValid(), tiny_gps.speed.mps(), tiny_gps.altitude.isValid(),
                       tiny_gps.altitude.meters());
  this->set_backup_location(
      tiny_gps.location.lat(), tiny_gps.location.lng(), tiny_gps.course.isValid(), tiny_gps.course.deg(),
      tiny_gps.satellites.isValid() ? uint8_t(std::min<uint32_t>(255, tiny_gps.satellites.value())) : 0);
}
#endif

void TugLogger::set_backup_fix(bool speed_valid, float speed_mps, bool altitude_valid, float altitude_m) {
  this->nmea_ms_ = millis();
  this->nmea_speed_valid_ = speed_valid;
  this->nmea_speed_mps_ = speed_mps;
  this->nmea_altitude_valid_ = altitude_valid;
  this->nmea_altitude_m_ = altitude_m;
}

void TugLogger::set_backup_location(double lat, double lon, bool course_valid, float course_deg, uint8_t satellites) {
  this->nmea_lat_ = lat;
  this->nmea_lon_ = lon;
  this->nmea_course_valid_ = course_valid;
  this->nmea_course_deg_ = course_deg;
  this->nmea_satellites_ = satellites;
}

#ifdef USE_TUG_LOGGER_POSITION_LINK
// Like a glider tracker's: every 5 s while flying, every 30 s on the ground, and with no fix as well, so the map
// shows the tug is there
void TugLogger::send_position_(const PositionSample &sample) {
  if (this->position_link_ == nullptr)
    return;
  const bool flying = this->detector_.is_flying();
  const uint32_t interval = flying ? POSITION_FLYING_MS : POSITION_GROUND_MS;
  if (this->position_sent_ && sample.now_ms - this->last_position_sent_ms_ < interval)
    return;
  this->position_sent_ = true;
  this->last_position_sent_ms_ = sample.now_ms;
  const Location &where = this->location_;
  PositionReport report;
  report.fix = where.valid;
  report.flying = flying;
  report.time = sample.epoch > 0 ? uint32_t(sample.epoch) : 0;
  if (where.valid) {
    report.lat = where.lat;
    report.lon = where.lon;
    report.alt_m = sample.geometric_valid ? int16_t(std::lround(sample.geometric_m)) : 0;
    report.speed_kmh = sample.speed_valid ? sample.ground_speed_mps * 3.6f : 0.0f;
    report.course_deg = where.course_valid ? where.course_deg : 0.0f;
    report.climb_cms = int16_t(std::lround(where.climb_mps * 100.0f));
  }
  report.satellites = where.satellites;
  report.battery_v = this->battery_voltage();
  uint8_t packet[PositionReport::SIZE];
  report.encode(packet);
  const bool ok = this->position_link_->send_to_interface(packet_interface::PacketBuffer(packet, sizeof(packet)), {});
  ESP_LOGV(TAG, "Position report sent: %s, fix %s", ok ? LOG_STR_LITERAL("ok") : LOG_STR_LITERAL("refused"),
           YESNO(where.valid));
}
#endif

// While on the ground: the airfield the next takeoff will be from
void TugLogger::choose_launch_field_() {
  const std::vector<Airfield> &airfields = this->provisioning_.airfields;
  if (airfields.empty()) {
    this->launch_field_name_.clear();
    this->detector_.set_launch_field(LaunchField::LAUNCH_FIELD_DEFAULT);
    return;
  }
  const Airfield *airfield =
      this->location_.valid ? nearest_airfield(airfields, this->location_.lat, this->location_.lon) : nullptr;
  if (airfield == nullptr) {
    this->launch_field_name_.clear();
    this->detector_.set_launch_field(LaunchField::LAUNCH_FIELD_UNKNOWN);
    return;
  }
  if (this->launch_field_name_ != airfield->name) {
    this->launch_field_name_ = airfield->name;
    ESP_LOGI(TAG, "At %s, elevation %.0f ft", airfield->name.c_str(), airfield->elevation_ft);
  }
  this->detector_.set_launch_field(LaunchField::LAUNCH_FIELD_KNOWN, airfield->elevation_ft);
}

void TugLogger::apply_fetched_airfields_() {
  if (!this->airfields_fetched_.load())
    return;
  std::lock_guard<std::mutex> guard(this->airfields_lock_);
  this->airfields_fetched_ = false;
  this->provisioning_.airfields = std::move(this->fetched_.airfields);
  this->provisioning_.field_elevation_ft = this->fetched_.field_elevation_ft;
  this->detector_.set_field_elevation_ft(this->provisioning_.field_elevation_ft);
  this->store_.set_default_field_elevation_ft(this->provisioning_.field_elevation_ft);
  ESP_LOGI(TAG, "%zu airfields fetched", this->provisioning_.airfields.size());
}

void TugLogger::save_flight_() {
  this->last_save_ms_ = millis();
  if (!this->store_.save_flight(this->detector_.flight(), this->flight_details_, this->epoch_())) {
    ESP_LOGW(TAG, "The flight could not be saved");
  }
}

void TugLogger::on_takeoff_() {
  ESP_LOGI(TAG, "Takeoff");
  this->flight_details_ = this->next_details_;
  const std::string tug_pilot = this->next_details_.tug_pilot;
  this->next_details_ = TowDetails{};
  this->next_details_.tug_pilot = tug_pilot;
  this->save_flight_();
  this->takeoff_callback_.call();
}

void TugLogger::on_landed_() {
  const FlightSnapshot &flight = this->detector_.flight();
  ESP_LOGI(TAG, "Landed after %" PRIu32 " s, highest %" PRId32 " ft MSL", flight.elapsed_s,
           this->detector_.to_altitude_ft(flight.max_height_ft));
  TowRecord record;
  record.launch = flight.launch;
  record.launch_ms = this->detector_.launch_ms();
  record.launch_ms_valid = flight.launch == 0;
  record.duration_s = flight.elapsed_s;
  record.climb_s = flight.climb_s;
  record.max_height_ft = flight.max_height_ft;
  record.field_elevation_ft = flight.field_elevation_ft;
  record.approximate = flight.approximate;
  record.details = this->flight_details_;
  this->store_.remember(record.details);
  this->record_flight_(record.duration_s);
  if (!this->store_.add_review(std::move(record))) {
    ESP_LOGW(TAG, "The tow could not be saved");
  }
  this->store_.clear_flight();
  this->landed_callback_.call();
}

void TugLogger::tick_() {
  const uint32_t now = millis();
  if (!this->store_ready_ && now - this->last_store_try_ms_ >= STORE_RETRY_MS) {
    this->last_store_try_ms_ = now;
    this->store_ready_ = this->start_store_();
  }

  if (this->store_ready_ && now - this->last_card_check_ms_ >= CARD_CHECK_MS) {
    this->last_card_check_ms_ = now;
    struct stat st;
    this->card_ok_ = stat((std::string(this->store_.root()) + "/tug").c_str(), &st) == 0;
  }

  const PositionSample sample = this->read_position_();
#ifdef USE_TUG_LOGGER_POSITION_LINK
  this->send_position_(sample);
#endif
  if (now - this->last_position_log_ms_ >= POSITION_LOG_MS) {
    this->last_position_log_ms_ = now;
    ESP_LOGD(TAG, "Position: source %u, speed %.1f m/s (%s), GPS altitude %.0f m (%s), pressure altitude %.0f m (%s)",
             unsigned(this->source_), sample.ground_speed_mps, YESNO(sample.speed_valid), sample.geometric_m,
             YESNO(sample.geometric_valid), sample.pressure_m, YESNO(sample.pressure_valid));
  }

  if (this->has_saved_flight_) {
    if (this->source_ == GpsSource::GPS_SOURCE_NONE && now < RESUME_WAIT_MS) {
      this->check_network_();
      return;
    }
    if (sample.speed_valid && sample.ground_speed_mps >= this->settings_.landing_speed_mps) {
      FlightSnapshot snapshot = this->saved_flight_;
      // The time the device was off still counts, if the clock can say how long that was
      if (sample.epoch != 0 && this->saved_at_ != 0 && sample.epoch > this->saved_at_)
        snapshot.elapsed_s += sample.epoch - this->saved_at_;
      snapshot.approximate = true;
      this->detector_.resume(snapshot, now);
      this->flight_details_ = this->saved_details_;
      this->has_saved_flight_ = false;
      ESP_LOGI(TAG, "Carrying on the flight");
      this->start_flight_log_("Carried on after a restart");
      this->takeoff_callback_.call();
    } else {
      this->close_saved_flight_();
    }
  }

  this->apply_fetched_airfields_();
  if (!this->detector_.is_flying())
    this->choose_launch_field_();
  const FlightEvent event = this->detector_.update(sample);
  this->log_sample_(sample, event);
  switch (event) {
    case FlightEvent::FLIGHT_EVENT_TAKEOFF:
      this->on_takeoff_();
      break;
    case FlightEvent::FLIGHT_EVENT_LANDED:
      this->on_landed_();
      break;
    case FlightEvent::FLIGHT_EVENT_NONE:
      break;
  }
  if (this->detector_.is_flying() && now - this->last_save_ms_ >= FLIGHT_SAVE_MS)
    this->save_flight_();
  if (sample.epoch != 0)
    this->store_.resolve_launch_times(sample.epoch, now);
  this->check_tow_watch_();
  if (this->store_ready_ && this->totals_.roll_to(this->today_()))
    this->store_.save_totals(this->totals_);

  this->check_network_();
  this->check_power_();
}

void TugLogger::check_tow_watch_() {
#ifdef USE_TUG_LOGGER_SKYECHO
  if (this->skyecho_ == nullptr)
    return;
  auto &watch = this->skyecho_->tow_watch();
  watch.set_enabled(this->detector_.is_flying());
  if (!this->detector_.is_flying() || !this->flight_details_.glider_registration.empty())
    return;
  std::string glider = glider_from_callsign(watch.get_callsign());
  if (glider.empty())
    return;
  ESP_LOGI(TAG, "%s has stayed behind, so it is the glider on tow", glider.c_str());
  this->flight_details_.glider_registration = std::move(glider);
  this->save_flight_();
#endif
}

void TugLogger::check_network_() {
  const uint32_t now = millis();
  if (now - this->last_network_check_ms_ < NETWORK_CHECK_MS)
    return;
  this->last_network_check_ms_ = now;
#ifdef USE_TUG_LOGGER_WIFI_NETWORKS
  if (this->wifi_networks_ != nullptr) {
    wifi_networks::NetworkInfo networks[wifi_networks::MAX_LISTED];
    const size_t count = this->wifi_networks_->get_networks(networks, wifi_networks::MAX_LISTED);
    bool skyecho = false;
    for (size_t i = 0; i != count; i++)
      skyecho = skyecho || (networks[i].connected && networks[i].skyecho);
    this->on_skyecho_network_ = skyecho;
  }
#endif
}

void TugLogger::check_power_() {
  if (this->external_power_ == nullptr || this->powering_off_)
    return;
  const uint32_t now = millis();
  const bool powered = this->external_power_->state;
  if (powered || this->detector_.is_flying() || this->has_saved_flight_) {
    this->power_lost_ms_ = 0;
    return;
  }
  if (this->power_lost_ms_ == 0)
    this->power_lost_ms_ = now;
  // The wait starts from the last use of the screen too
  if (now - this->power_lost_ms_ < this->shutdown_window_ms_ ||
      now - this->last_activity_ms_ < this->shutdown_window_ms_)
    return;
  ESP_LOGI(TAG, "Idle on battery: powering off");
  this->powering_off_ = true;
  App.run_safe_shutdown_hooks();
  this->power_off_callback_.call();
}

}  // namespace esphome::tug_logger

#endif  // USE_ESP32
