#include "bms_charger.h"
#include "esphome/core/hal.h"

#include <cmath>

namespace esphome {
namespace {

const uint8_t BMS_NAME[] = {'E', 'S', 'P', 'H', 'o', 'm', 'e'};

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

void put_int16(float value, std::vector<uint8_t> &buffer, const float scale) {
  if (std::isnan(value))
    value = 0.0;
  int16_t const int_value = (int16_t) (value / scale);
  buffer.push_back((uint8_t) (int_value & 0xFF));
  buffer.push_back((uint8_t) ((int_value >> 8) & 0xFF));
}

void log_msg(const char *text, uint32_t id, std::vector<uint8_t> data) {
  size_t const len = std::min(data.size(), 8U);
  char buffer[32];
  for (size_t i = 0; i != len; i++) {
    sprintf(buffer + i * 3, "%02X ", data[i]);
  }
  ESP_LOGD(TAG, "%s 0x%X: %s", text, id, buffer);
}

uint8_t flag_bit(uint32_t pos, bool set) {
  if (set)
    return 1 << pos;
  return 1 << (pos + 1);
}

uint32_t sma_alarms(std::vector<uint8_t> &data, uint32_t alarms, uint32_t warnings) {
  data.clear();
  uint8_t byte = 0;

  byte |= flag_bit(0, canbus_bms::FLAG_GENERAL_ALARM & alarms);
  byte |= flag_bit(2, canbus_bms::FLAG_HIGH_VOLTAGE & alarms);
  byte |= flag_bit(4, canbus_bms::FLAG_LOW_VOLTAGE & alarms);
  byte |= flag_bit(6, canbus_bms::FLAG_HIGH_TEMPERATURE & alarms);
  data.push_back(byte);
  byte = 0;
  byte |= flag_bit(0, canbus_bms::FLAG_LOW_TEMPERATURE & alarms);
  byte |= flag_bit(2, canbus_bms::FLAG_HIGH_TEMPERATURE_CHARGE & alarms);
  byte |= flag_bit(4, canbus_bms::FLAG_LOW_TEMPERATURE_CHARGE & alarms);
  byte |= flag_bit(6, canbus_bms::FLAG_HIGH_CURRENT & alarms);
  data.push_back(byte);
  byte = 0;
  byte |= flag_bit(0, canbus_bms::FLAG_HIGH_CURRENT_CHARGE & alarms);
  byte |= flag_bit(2, canbus_bms::FLAG_CONTACTOR_ERROR & alarms);
  byte |= flag_bit(4, canbus_bms::FLAG_SHORT_CIRCUIT & alarms);
  byte |= flag_bit(6, canbus_bms::FLAG_BMS_INTERNAL_ERROR & alarms);
  data.push_back(byte);
  byte = 0;
  byte |= flag_bit(0, canbus_bms::FLAG_CELL_IMBALANCE & alarms);
  data.push_back(byte);

  byte = 0;
  byte |= flag_bit(0, canbus_bms::FLAG_GENERAL_ALARM & warnings);
  byte |= flag_bit(2, canbus_bms::FLAG_HIGH_VOLTAGE & warnings);
  byte |= flag_bit(4, canbus_bms::FLAG_LOW_VOLTAGE & warnings);
  byte |= flag_bit(6, canbus_bms::FLAG_HIGH_TEMPERATURE & warnings);
  data.push_back(byte);
  byte = 0;
  byte |= flag_bit(0, canbus_bms::FLAG_LOW_TEMPERATURE & warnings);
  byte |= flag_bit(2, canbus_bms::FLAG_HIGH_TEMPERATURE_CHARGE & warnings);
  byte |= flag_bit(4, canbus_bms::FLAG_LOW_TEMPERATURE_CHARGE & warnings);
  byte |= flag_bit(6, canbus_bms::FLAG_HIGH_CURRENT & warnings);
  data.push_back(byte);
  byte = 0;
  byte |= flag_bit(0, canbus_bms::FLAG_HIGH_CURRENT_CHARGE & warnings);
  byte |= flag_bit(2, canbus_bms::FLAG_CONTACTOR_ERROR & warnings);
  byte |= flag_bit(4, canbus_bms::FLAG_SHORT_CIRCUIT & warnings);
  byte |= flag_bit(6, canbus_bms::FLAG_BMS_INTERNAL_ERROR & warnings);
  data.push_back(byte);
  byte = 0;
  byte |= flag_bit(0, canbus_bms::FLAG_CELL_IMBALANCE & warnings);
  data.push_back(byte);
  return SMA_ALARMS_MSG;
}

uint32_t pylon_alarms(std::vector<uint8_t> &data, uint32_t alarms, uint32_t warnings) {
  data.clear();
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
  data.push_back(byte);
  byte = 0;
  if (warnings & canbus_bms::FLAG_BMS_INTERNAL_ERROR)
    byte |= 0x08;
  if (warnings & canbus_bms::FLAG_HIGH_CURRENT_CHARGE)
    byte |= 0x01;
  data.push_back(byte);
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
  data.push_back(byte);
  byte = 0;
  if (alarms & canbus_bms::FLAG_BMS_INTERNAL_ERROR)
    byte |= 0x08;
  if (alarms & canbus_bms::FLAG_HIGH_CURRENT_CHARGE)
    byte |= 0x01;
  data.push_back(byte);
  data.push_back(0x01);
  data.push_back('P');
  data.push_back('N');
  data.push_back(0);
  return PYLON_ALARMS_MSG;
}
}  // namespace

namespace bms_charger {

void BmsChargerComponent::play(std::vector<uint8_t> data, uint32_t can_id, bool remote_transmission_request) {
  this->last_rx_ = millis();
  if (this->debug_)
    log_msg("Received from inverter", can_id, data);
}

// construct data for sma alarms message

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
    ESP_LOGD(TAG, "MaxVoltages.size() = %d", max_voltages.size());

  std::vector<uint8_t> data;
  if (this->counter_ % ALARMS_INTERVAL == 0) {
    uint32_t msg_id = 0;
    switch (this->protocol_) {
      case PROTOCOL_SMA:
        msg_id = sma_alarms(data, alarms, warnings);
        break;

      default:
        msg_id = pylon_alarms(data, alarms, warnings);
        break;
    }
    this->canbus_->send_data(msg_id, false, false, data);
    if (this->debug_) {
      ESP_LOGI(TAG, "alarms = 0x%04X, warnings=0x%04X", alarms, warnings);
      log_msg("Alarms", msg_id, data);
    }
  }

  if (this->counter_ % REQUESTS_INTERVAL == 0 && this->protocol_ == PROTOCOL_PYLON) {
    data.clear();
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

  float acc = 0.0;
  if (this->counter_ % STATUS_INTERVAL == 0 && !voltages.empty()) {
    data.clear();
    // Take the highest measured voltage.
    for (auto value : voltages)
      acc = std::max(acc, value);
    float const voltage = acc;
    put_int16(voltage, data, 0.01);

    // sum currents, resolution 0.1
    acc = 0.0;
    for (auto value : currents)
      acc += value;
    float const current = acc;
    put_int16(current, data, 0.1);

    // average temperatures, resolution 0.1
    acc = 0.0;
    for (auto value : temperatures)
      acc += value;
    float const temperature = acc / temperatures.size();
    put_int16(temperature, data, 0.1);

    this->canbus_->send_data(STATUS_MSG, false, false, data);
    if (this->debug_) {
      ESP_LOGI(TAG, "Voltage=%.1f, current=%.1f, temperature=%.1f", voltage, current, temperature);
      log_msg("Status", STATUS_MSG, data);
    }
  }

  if (this->counter_ % CHARGE_INTERVAL == 1 && !charges.empty()) {
    data.clear();
    // average charge
    acc = 0.0;
    for (auto value : charges)
      acc += value;
    acc /= charges.size();
    this->last_charge_ = std::round(acc);
    put_int16(acc, data, 1.0);

    // average health
    acc = 0.0;
    for (auto value : healths)
      acc += value;
    float const health = acc / healths.size();
    put_int16(health, data, 1.0);
    this->canbus_->send_data(CHARGE_MSG, false, false, data);
    if (this->debug_) {
      ESP_LOGI(TAG, "Charge=%d%%, health=%.0f%%", this->last_charge_, health);
      log_msg("Charge", CHARGE_MSG, data);
    }
  }

  // send charge/discharge limits

  if (this->counter_ % LIMITS_INTERVAL == 2 && !max_voltages.empty()) {
    // max voltage is the highest reported. TODO is this the best choice? Using the lowest may compromise balancing.
    data.clear();
    acc = 0.0;
    for (auto value : max_voltages)
      acc = std::max(acc, value);
    float max_voltage = acc;
    if (this->max_charge_voltage_number_ != nullptr)
      max_voltage = std::max(max_voltage, this->max_charge_voltage_number_->state);
    put_int16(max_voltage, data, 0.1);

    // max charge current is the lowest value times the number of batteries.
    // or the average value, whichever is greater
    // TODO - dynamically adjust this to keep all batteries within their limits.
    acc = 10000.0;
    float avg = 0.0;
    for (auto value : max_charge_currents) {
      acc = std::min(acc, value);
      avg += value;
    }
    float max_charge = std::max(acc * max_charge_currents.size(), avg / max_charge_currents.size());
    if (this->max_charge_current_number_ != nullptr)
      max_charge = std::min(max_charge, this->max_charge_current_number_->state);
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
      ESP_LOGD(TAG, "charge point %d, x = %f, max_charge = %f", i, x, max_charge);
    }
    put_int16(max_charge, data, 0.1);

    // similarly with discharge currents
    acc = 10000.0;
    for (auto value : max_discharge_currents)
      acc = std::min(acc, value);
    float max_discharge = acc * max_discharge_currents.size();
    if (this->max_discharge_current_number_ != nullptr)
      max_discharge = std::min(max_discharge, this->max_discharge_current_number_->state);
    put_int16(max_discharge, data, 0.1);

    acc = 0.0;
    // use the highest minimum voltage - this is a conservative choice. Should have little downside.
    for (auto value : min_voltages)
      acc = std::max(acc, value);
    float const min_voltage = acc;
    put_int16(min_voltage, data, 0.1);

    this->canbus_->send_data(LIMITS_MSG, false, false, data);
    if (this->debug_) {
      ESP_LOGD(TAG, "Max volts=%.1f, max charge=%.1f, max discharge=%.1f, min volts=%.1f", max_voltage, max_charge,
               max_discharge, min_voltage);
      log_msg("Limits", LIMITS_MSG, data);
    }
  }

  // send name
  data.clear();
  data.insert(data.end(), &BMS_NAME[0], &BMS_NAME[sizeof(BMS_NAME)]);
  if (this->counter_ % NAME_INTERVAL == 0) {
    this->canbus_->send_data(NAME_MSG, false, false, data);
    if (this->debug_)
      log_msg("Name", NAME_MSG, data);
  }

  // send heartbeats to the batteries every time, if we are connected to a charger or inverter.
  if (now_connected) {
    for (auto *desc : batteries_) {
      data.clear();
      data.insert(data.end(), (uint8_t *) &desc->heartbeat_text_[0],
                  (uint8_t *) &desc->heartbeat_text_[strlen(desc->heartbeat_text_)]);
      desc->battery_->send_data(desc->heartbeat_id_, false, false, data);
      if (this->debug_)
        log_msg("Heartbeat", desc->heartbeat_id_, data);
    }
  }
}

}  // namespace bms_charger
}  // namespace esphome
