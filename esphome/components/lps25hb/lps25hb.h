#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace lps25hb {


class LPS25HBComponent
  : public PollingComponent,
    public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_HIGH,
      spi::CLOCK_PHASE_TRAILING, spi::DATA_RATE_1MHZ>,
    public sensor::Sensor {
 public:
  /// Schedule temperature+pressure readings.
  void update() override;
  /// Setup the sensor and test for a connection.
  void setup() override;
  void dump_config() override;

  float get_setup_priority() const override;

  uint8_t read_register(uint8_t address);

 protected:
  /// Internal method to read the pressure from the component after it has been scheduled.
  void read_pressure_();
  void write_register(uint8_t address, uint8_t data);
};

}  // namespace lps25hb
}  // namespace esphome
