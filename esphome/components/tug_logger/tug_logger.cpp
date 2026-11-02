#include "esphome/core/defines.h"

#ifdef USE_ESP32

#include "tug_logger.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <sys/stat.h>

#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include "tow_uploader.h"

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

void TugLogger::setup() {
  this->detector_ = FlightDetector(this->settings_);
  this->last_activity_ms_ = millis();
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

void TugLogger::loop() {
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

bool TugLogger::start_store_() {
  if (!this->store_.begin())
    return false;
  this->card_ok_ = true;
  this->provisioned_ = this->store_.load_provisioning(this->provisioning_);
  if (this->provisioned_) {
    this->detector_.set_field_elevation_ft(this->provisioning_.field_elevation_ft);
    ESP_LOGI(TAG, "Tug %s, field elevation %.0f ft", this->provisioning_.tug_registration.c_str(),
             this->provisioning_.field_elevation_ft);
  } else {
    ESP_LOGW(TAG, "No usable tug.json or tug-*.json in the root of the SD card");
  }
  this->queued_ = this->store_.queued_count();
  this->failed_ = this->store_.failed_count();
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
  record.approximate = true;
  record.details = this->saved_details_;
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
#ifdef USE_TUG_LOGGER_SKYECHO
  if (this->skyecho_ != nullptr) {
    skyecho::OwnshipT ownship;
    if (this->skyecho_->get_ownship_position(&ownship)) {
      this->source_ = GpsSource::GPS_SOURCE_SKYECHO;
      sample.speed_valid = ownship.report.isGroundSpeedValid;
      sample.ground_speed_mps = ownship.report.groundSpeed;
      sample.pressure_valid = ownship.report.isAltitudeValid;
      sample.pressure_m = ownship.report.altitude;
      sample.geometric_valid = this->skyecho_->get_ownship_geo_altitude(&sample.geometric_m);
      return sample;
    }
  }
#endif
#ifdef USE_TUG_LOGGER_GPS
  if (this->nmea_ms_ != 0 && sample.now_ms - this->nmea_ms_ < NMEA_MAX_AGE_MS) {
    this->source_ = GpsSource::GPS_SOURCE_NMEA;
    sample.speed_valid = this->nmea_speed_valid_;
    sample.ground_speed_mps = this->nmea_speed_mps_;
    sample.geometric_valid = this->nmea_altitude_valid_;
    sample.geometric_m = this->nmea_altitude_m_;
  }
#endif
  return sample;
}

#ifdef USE_TUG_LOGGER_GPS
void TugLogger::on_update(TinyGPSPlus &tiny_gps) {
  if (!tiny_gps.location.isValid())
    return;
  this->nmea_ms_ = millis();
  this->nmea_speed_valid_ = tiny_gps.speed.isValid();
  this->nmea_speed_mps_ = tiny_gps.speed.mps();
  this->nmea_altitude_valid_ = tiny_gps.altitude.isValid();
  this->nmea_altitude_m_ = tiny_gps.altitude.meters();
}
#endif

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
  ESP_LOGI(TAG, "Landed after %" PRIu32 " s, highest %" PRId32 " ft", flight.elapsed_s, flight.max_height_ft);
  TowRecord record;
  record.launch = flight.launch;
  record.launch_ms = this->detector_.launch_ms();
  record.launch_ms_valid = flight.launch == 0;
  record.duration_s = flight.elapsed_s;
  record.climb_s = flight.climb_s;
  record.max_height_ft = flight.max_height_ft;
  record.approximate = flight.approximate;
  record.details = this->flight_details_;
  this->store_.remember(record.details);
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
      this->takeoff_callback_.call();
    } else {
      this->close_saved_flight_();
    }
  }

  switch (this->detector_.update(sample)) {
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

  this->check_network_();
  this->check_power_();
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
