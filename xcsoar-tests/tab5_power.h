#pragma once

// The Tab5 charges its battery only while it is switched on, so with a supply connected it sleeps instead of
// switching off, and wakes each minute to check that the supply is still there.

#include <cmath>

#include <driver/gpio.h>
#include <esp_sleep.h>

#include "esphome/components/ina226/ina226.h"
#include "esphome/components/output/binary_output.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/hal.h"

namespace tab5_power {

static constexpr gpio_num_t BACKLIGHT_PIN = GPIO_NUM_22;
static constexpr uint64_t CHECK_INTERVAL_US = 60ULL * 1000 * 1000;
// Running from the battery alone the board draws 0.26 A at the start-up check and 0.33 A with XCSoar; charging,
// or on a weak supply, under 0.1 A
static constexpr float SUPPLY_MAX_BATTERY_CURRENT = 0.15f;
// The INA226 averages 16 conversions of about 2 ms each after its setup
static constexpr uint32_t FIRST_READING_DELAY_MS = 100;

inline bool woken_by_timer() { return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER; }

/// Called before the backlight is set up. After a timer wake the backlight stays held off.
inline void release_backlight() {
  if (!woken_by_timer())
    gpio_hold_dis(BACKLIGHT_PIN);
}

/// The battery current now, positive while the battery discharges.
inline float read_battery_current(esphome::ina226::INA226Component *monitor, esphome::sensor::Sensor *current) {
  monitor->update();
  return current->state;
}

/// The battery current at the start-up check, once the INA226 has its first reading.
inline float read_start_current(esphome::ina226::INA226Component *monitor, esphome::sensor::Sensor *current) {
  esphome::delay(FIRST_READING_DELAY_MS);
  return read_battery_current(monitor, current);
}

/// Sleep with the backlight held off; the next start is a timer wake.
[[noreturn]] inline void sleep(esphome::switch_::Switch *wifi_power) {
  wifi_power->turn_off();
  // Unheld, the pin floats in deep sleep
  gpio_reset_pin(BACKLIGHT_PIN);
  gpio_set_direction(BACKLIGHT_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(BACKLIGHT_PIN, 0);
  gpio_hold_en(BACKLIGHT_PIN);
  esp_sleep_enable_timer_wakeup(CHECK_INTERVAL_US);
  esp_deep_sleep_start();
}

/// Sleep when the battery current shows a supply, otherwise switch off. Returns only if the switch-off failed.
inline void sleep_or_switch_off(float battery_current, esphome::switch_::Switch *wifi_power,
                                esphome::output::BinaryOutput *power_off) {
  if (!std::isnan(battery_current) && battery_current < SUPPLY_MAX_BATTERY_CURRENT)
    sleep(wifi_power);
  // A pulse switches the Tab5 off, as a double press of the side button does; the button starts it again
  for (int i = 0; i != 3; i++) {
    power_off->turn_on();
    esphome::delay(100);
    power_off->turn_off();
    esphome::delay(100);
  }
  gpio_hold_dis(BACKLIGHT_PIN);
}

}  // namespace tab5_power
