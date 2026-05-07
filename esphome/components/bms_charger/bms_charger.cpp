#include "bms_charger.h"
#include "esphome/core/hal.h"

#include <cmath>
#include <numeric>

namespace esphome {
namespace bms_charger {

using namespace bytebuffer;
using std::begin;
using std::end;
static const std::vector<uint8_t> BMS_NAME{'E', 'S', 'P', 'H', 'o', 'm', 'e'};

// intervals for various messages. Could be made config values if required.

const size_t NAME_INTERVAL = 30;
const size_t LIMITS_INTERVAL = 3;
const size_t CHARGE_INTERVAL = 3;
const size_t STATUS_INTERVAL = 3;
const size_t ALARMS_INTERVAL = 11;
const size_t REQUESTS_INTERVAL = 5;

// message types

const uint32_t NAME_MSG = 0x35E;
const uint32_t LIMITS_MSG = 0x351;
const uint32_t SMA_ALARMS_MSG = 0x35A;
const uint32_t PYLON_ALARMS_MSG = 0x359;
const uint32_t CHARGE_MSG = 0x355;
const uint32_t STATUS_MSG = 0x356;
const uint32_t REQUEST_MSG = 0x35C;
// static const uint32_t INFO_MSG = 0x35F;     // TODO
const char *const TAG = "BmsCharger";

void update_list(std::vector<float> &list, float value) {
  if (!std::isnan(value))
    list.push_back(value);
}

void log_msg(const char *text, uint32_t id, std::vector<uint8_t> data) {
  char hexbuf[format_hex_pretty_size(data.size())];
  format_hex_pretty_to(hexbuf, sizeof hexbuf, data.data(), data.size());
  ESP_LOGD(TAG, "%s 0x%" PRIu32 ": %s", text, id, hexbuf);
}

uint8_t flag_bit(uint32_t pos, bool set) {
  if (set)
    return 1 << pos;
  return 1 << (pos + 1);
}

ByteBuffer sma_alarms(uint32_t alarms, uint32_t warnings) {
  auto data = ByteBuffer(8);
  uint8_t byte;

  byte = flag_bit(0, canbus_bms::FLAG_GENERAL_ALARM & alarms);
  byte |= flag_bit(2, canbus_bms::FLAG_HIGH_VOLTAGE & alarms);
  byte |= flag_bit(4, canbus_bms::FLAG_LOW_VOLTAGE & alarms);
  byte |= flag_bit(6, canbus_bms::FLAG_HIGH_TEMPERATURE & alarms);
  data.put(byte);
  byte = flag_bit(0, canbus_bms::FLAG_LOW_TEMPERATURE & alarms);
  byte |= flag_bit(2, canbus_bms::FLAG_HIGH_TEMPERATURE_CHARGE & alarms);
  byte |= flag_bit(4, canbus_bms::FLAG_LOW_TEMPERATURE_CHARGE & alarms);
  byte |= flag_bit(6, canbus_bms::FLAG_HIGH_CURRENT & alarms);
  data.put(byte);
  byte = flag_bit(0, canbus_bms::FLAG_HIGH_CURRENT_CHARGE & alarms);
  byte |= flag_bit(2, canbus_bms::FLAG_CONTACTOR_ERROR & alarms);
  byte |= flag_bit(4, canbus_bms::FLAG_SHORT_CIRCUIT & alarms);
  byte |= flag_bit(6, canbus_bms::FLAG_BMS_INTERNAL_ERROR & alarms);
  data.put(byte);
  byte = flag_bit(0, canbus_bms::FLAG_CELL_IMBALANCE & alarms);
  data.put(byte);

  byte = flag_bit(0, canbus_bms::FLAG_GENERAL_ALARM & warnings);
  byte |= flag_bit(2, canbus_bms::FLAG_HIGH_VOLTAGE & warnings);
  byte |= flag_bit(4, canbus_bms::FLAG_LOW_VOLTAGE & warnings);
  byte |= flag_bit(6, canbus_bms::FLAG_HIGH_TEMPERATURE & warnings);
  data.put(byte);
  byte = flag_bit(0, canbus_bms::FLAG_LOW_TEMPERATURE & warnings);
  byte |= flag_bit(2, canbus_bms::FLAG_HIGH_TEMPERATURE_CHARGE & warnings);
  byte |= flag_bit(4, canbus_bms::FLAG_LOW_TEMPERATURE_CHARGE & warnings);
  byte |= flag_bit(6, canbus_bms::FLAG_HIGH_CURRENT & warnings);
  data.put(byte);
  byte = flag_bit(0, canbus_bms::FLAG_HIGH_CURRENT_CHARGE & warnings);
  byte |= flag_bit(2, canbus_bms::FLAG_CONTACTOR_ERROR & warnings);
  byte |= flag_bit(4, canbus_bms::FLAG_SHORT_CIRCUIT & warnings);
  byte |= flag_bit(6, canbus_bms::FLAG_BMS_INTERNAL_ERROR & warnings);
  data.put(byte);
  byte = flag_bit(0, canbus_bms::FLAG_CELL_IMBALANCE & warnings);
  data.put(byte);
  return data;
}

ByteBuffer pylon_alarms(uint32_t alarms, uint32_t warnings) {
  auto data = ByteBuffer(8);
  uint8_t byte = 0;
  if (warnings & canbus_bms::FLAG_HIGH_CURRENT)
    byte |= 0x80;
  if (warnings & canbus_bms::FLAG_LOW_TEMPERATURE)
    byte |= 0x10;
  if (warnings & canbus_bms::FLAG_HIGH_TEMPERATURE)
    byte |= 0x08;
  if (warnings & canbus_bms::FLAG_LOW_VOLTAGE)
    byte |= 0x04;
  if (warnings & canbus_bms::FLAG_HIGH_VOLTAGE)
    byte |= 0x02;
  data.put(byte);
  byte = 0;
  if (warnings & canbus_bms::FLAG_BMS_INTERNAL_ERROR)
    byte |= 0x08;
  if (warnings & canbus_bms::FLAG_HIGH_CURRENT_CHARGE)
    byte |= 0x01;
  data.put(byte);
  byte = 0;
  if (alarms & canbus_bms::FLAG_HIGH_CURRENT)
    byte |= 0x80;
  if (alarms & canbus_bms::FLAG_LOW_TEMPERATURE)
    byte |= 0x10;
  if (alarms & canbus_bms::FLAG_HIGH_TEMPERATURE)
    byte |= 0x08;
  if (alarms & canbus_bms::FLAG_LOW_VOLTAGE)
    byte |= 0x04;
  if (alarms & canbus_bms::FLAG_HIGH_VOLTAGE)
    byte |= 0x02;
  data.put(byte);
  byte = 0;
  if (alarms & canbus_bms::FLAG_BMS_INTERNAL_ERROR)
    byte |= 0x08;
  if (alarms & canbus_bms::FLAG_HIGH_CURRENT_CHARGE)
    byte |= 0x01;
  data.put(byte);
  data.put(0x01);
  data.put('P');
  data.put('N');
  data.put(0x00);
  return data;
}

void BmsChargerComponent::play(const std::vector<uint8_t> &data, const uint32_t &can_id,
                               const bool &remote_transmission_request) {
  this->last_rx_ = millis();
  if (this->debug_)
    log_msg("Received from inverter", can_id, data);
}

static float max_current(std::vector<float> &currents, const number::Number *limit) {
  if (currents.empty())
    return 0.0f;
  float min = *std::min(begin(currents), end(currents)) * currents.size();
  float value = std::accumulate(currents.begin(), currents.end(), 0.0f) / currents.size();
  value = std::max(min, value);
  if (limit != nullptr)
    value = std::min(value, limit->state);
  return value;
}

void BmsChargerComponent::send_status(std::vector<float> &voltages, std::vector<float> &currents,
                                      std::vector<float> &temperatures) const {
  auto buffer = ByteBuffer(6);
  if (!voltages.empty()) {
    float voltage, current, temperature;
    // Take the highest measured voltage.
    buffer.put_uint16((voltage = *std::max_element(begin(voltages), end(voltages))) * 100.0f);
    // sum currents
    buffer.put_int16((current = std::accumulate(begin(currents), end(currents), 0.0f)) * 10.0f);
    buffer.put_int16((temperature = std::accumulate(begin(temperatures), end(temperatures), 0.0f)) * 10.0f /
                     temperatures.size());
    if (this->debug_) {
      ESP_LOGI(TAG, "Voltage=%.1f, current=%.1f, temperature=%.1f", voltage, current, temperature);
      log_msg("Status", STATUS_MSG, buffer.get_data());
    }
  }
  this->canbus_->send_data(STATUS_MSG, false, false, buffer.get_data());
}
// called at a typically 1 second interval
void BmsChargerComponent::update() {
  this->counter_++;

  std::vector<float> voltages;
  std::vector<float> currents;
  std::vector<float> charges;
  std::vector<float> temperatures;
  std::vector<float> healths;
  std::vector<float> max_voltages;
  std::vector<float> min_voltages;
  std::vector<float> max_charge_currents;
  std::vector<float> max_discharge_currents;

  bool const now_connected = this->last_rx_ + this->timeout_ > millis();
  if (this->connectivity_sensor_)
    this->connectivity_sensor_->publish_state(now_connected);

  // this loop could be broken up and only the parts necessary done on each update() call,
  // but the time used here is not that significant, unlike the CAN send_message() calls.
  uint32_t warnings = 0;
  uint32_t alarms = 0;
  uint32_t requests = 0;

  for (BatteryDesc *battery : batteries_) {
    // calculate average battery voltage
    canbus_bms::CanbusBmsComponent *bms = battery->battery_;
    update_list(voltages, bms->get_voltage());
    update_list(currents, bms->get_current());
    update_list(charges, bms->get_charge());
    update_list(temperatures, bms->get_temperature());
    update_list(healths, bms->get_health());
    update_list(max_voltages, bms->get_max_voltage());
    update_list(min_voltages, bms->get_min_voltage());
    update_list(max_charge_currents, bms->get_max_charge_current());
    update_list(max_discharge_currents, bms->get_max_discharge_current());
    alarms |= bms->get_alarms();
    warnings |= bms->get_warnings();
    requests |= bms->get_requests();
    if (this->get_switch_state_(SW_SUPPRESS_OVP)) {
      alarms &= ~canbus_bms::FLAG_HIGH_VOLTAGE;
      warnings &= ~canbus_bms::FLAG_HIGH_VOLTAGE;
    }
  }
  if (this->debug_)
    ESP_LOGD(TAG, "MaxVoltages.size() = %zu", max_voltages.size());

  if (this->counter_ % ALARMS_INTERVAL == 0) {
    uint32_t msg_id;
    switch (this->protocol_) {
      case PROTOCOL_SMA: {
        auto data = sma_alarms(alarms, warnings);
        this->canbus_->send_data(SMA_ALARMS_MSG, false, false, data.get_data());
        break;
      }

      default: {
        auto data = pylon_alarms(alarms, warnings);
        this->canbus_->send_data(PYLON_ALARMS_MSG, false, false, data.get_data());
        break;
      }
    }
    if (this->debug_) {
      ESP_LOGI(TAG, "alarms = 0x%04X, warnings=0x%04X", alarms, warnings);
    }
  }

  if (this->counter_ % STATUS_INTERVAL == 0) {
    send_status(voltages, currents, temperatures);
  }

  if (!voltages.empty()) {
    if (this->counter_ % REQUESTS_INTERVAL == 0 && this->protocol_ == PROTOCOL_PYLON) {
      auto data = std::vector<uint8_t>();
      uint8_t byte = 0;
      if (requests & 1u << canbus_bms::REQ_CHARGE_ENABLE && !this->get_switch_state_(SW_NO_CHARGE))
        byte |= 0x80;
      if (requests & 1u << canbus_bms::REQ_DISCHARGE_ENABLE && !this->get_switch_state_(SW_NO_DISCHARGE))
        byte |= 0x40;
      if (requests & 1u << canbus_bms::REQ_FORCE_CHARGE_1 || this->get_switch_state_(SW_FORCE_CHARGE_1))
        byte |= 0x20;
      if (requests & 1u << canbus_bms::REQ_FORCE_CHARGE_2)
        byte |= 0x10;
      if (requests & 1u << canbus_bms::REQ_FULL_CHARGE || this->get_switch_state_(SW_FULL_CHARGE))
        byte |= 0x08;
      data.push_back(byte);
      data.push_back(0);
      this->canbus_->send_data(REQUEST_MSG, false, false, data);
      if (this->debug_) {
        ESP_LOGI(TAG, "requests = 0x%04X", requests);
        log_msg("Requests", REQUEST_MSG, data);
      }
    }
  }

  if (this->counter_ % CHARGE_INTERVAL == 1 && !charges.empty()) {
    auto buffer = ByteBuffer(4);
    float health, charge;
    buffer.put_uint16((charge = std::accumulate(begin(charges), end(charges), 0.0f) / charges.size()));
    buffer.put_uint16(health = std::accumulate(begin(healths), end(healths), 0.0f) / healths.size());
    this->last_charge_ = charge;
    // average health
    this->canbus_->send_data(CHARGE_MSG, false, false, buffer.get_data());
    if (this->debug_) {
      ESP_LOGI(TAG, "Charge=%d%%, health=%.0f%%", this->last_charge_, health);
      log_msg("Charge", CHARGE_MSG, buffer.get_data());
    }
  }

  // send charge/discharge limits

  if (this->counter_ % LIMITS_INTERVAL == 2 && !max_voltages.empty()) {
    // max voltage is the highest reported. TODO is this the best choice? Using the lowest may compromise balancing.
    auto buffer = ByteBuffer(8);
    float max_voltage = *std::max(begin(max_voltages), end(max_voltages));
    if (this->max_charge_voltage_number_ != nullptr)
      max_voltage = std::max(max_voltage, this->max_charge_voltage_number_->state);
    buffer.put_uint16(max_voltage * 10.0f);

    // max charge current is the lowest value times the number of batteries.
    // or the average value, whichever is greater
    // TODO - dynamically adjust this to keep all batteries within their limits.
    float max_charge = max_current(max_charge_currents, this->max_charge_current_number_);
    if (!this->charge_points_.empty()) {
      auto chg = this->last_charge_ / 100.0f;
      size_t i = 0;
      for (; i != this->charge_points_.size() - 1; i++) {
        if (chg > this->charge_points_[i].first && chg <= this->charge_points_[i + 1].first)
          break;
      }
      auto lower = this->charge_points_[i];
      auto upper = lower;
      if (i != this->charge_points_.size()) {
        upper = this->charge_points_[i + 1];
      }
      auto x = lower.second + (chg - lower.first) * (upper.second - lower.second) / (upper.first - lower.first);
      max_charge = std::min(max_charge, x);
      ESP_LOGD(TAG, "charge point %zu, x = %f, max_charge = %f", i, x, max_charge);
    }
    buffer.put_uint16(max_charge * 10.0);

    // similarly with discharge currents
    float max_discharge = max_current(max_discharge_currents, this->max_discharge_current_number_);
    buffer.put_uint16(max_discharge * 10.0);

    float min_voltage;
    buffer.put_uint16((min_voltage = *std::max(begin(min_voltages), end(min_voltages))) * 10.0f);

    this->canbus_->send_data(LIMITS_MSG, false, false, buffer.get_data());
    if (this->debug_) {
      ESP_LOGD(TAG, "Max volts=%.1f, max charge=%.1f, max discharge=%.1f, min volts=%.1f", max_voltage, max_charge,
               max_discharge, min_voltage);
      log_msg("Limits", LIMITS_MSG, buffer.get_data());
    }
  }

  // send name
  if (this->counter_ % NAME_INTERVAL == 0) {
    this->canbus_->send_data(NAME_MSG, false, false, BMS_NAME);
    if (this->debug_)
      log_msg("Name", NAME_MSG, BMS_NAME);
  }

  // send heartbeats to the batteries every time, if we are connected to a charger or inverter.
  if (now_connected) {
    for (auto *desc : batteries_) {
      desc->battery_->send_data(desc->heartbeat_id_, false, false, desc->heartbeat_text_);
      if (this->debug_)
        log_msg("Heartbeat", desc->heartbeat_id_, desc->heartbeat_text_);
    }
  }
}

}  // namespace bms_charger
}  // namespace esphome
