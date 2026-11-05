#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/components/motion/motion_component.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

namespace esphome::icm20948 {

// Accelerometer full-scale range: ACCEL_FS_SEL, bits [2:1] of ACCEL_CONFIG
enum ICM20948AccelRange : uint8_t {
  ICM20948_ACCEL_RANGE_2G = 0x00,
  ICM20948_ACCEL_RANGE_4G = 0x01,
  ICM20948_ACCEL_RANGE_8G = 0x02,
  ICM20948_ACCEL_RANGE_16G = 0x03,
};

// Gyroscope full-scale range: GYRO_FS_SEL, bits [2:1] of GYRO_CONFIG_1
enum ICM20948GyroRange : uint8_t {
  ICM20948_GYRO_RANGE_250 = 0x00,
  ICM20948_GYRO_RANGE_500 = 0x01,
  ICM20948_GYRO_RANGE_1000 = 0x02,
  ICM20948_GYRO_RANGE_2000 = 0x03,
};

/// A magnetometer reading in µT, in the axes of the accelerometer; an axis is NaN on overflow.
struct MagneticField {
  float x;
  float y;
  float z;
};

/// The accelerometer, gyroscope, temperature sensor and AK09916 magnetometer of a TDK InvenSense ICM-20948.
class ICM20948Component final : public motion::MotionComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;

  void set_accel_range(ICM20948AccelRange range) { this->accel_range_ = range; }
  void set_gyro_range(ICM20948GyroRange range) { this->gyro_range_ = range; }
#ifdef USE_ICM20948_TEMPERATURE
  HUB_PUBLISHER(temperature, float)
#endif
#ifdef USE_ICM20948_MAGNETOMETER
  HUB_PUBLISHER(magnetometer, const MagneticField &)
#endif

 protected:
  bool update_data(motion::MotionData &data) override;
  bool select_bank_(uint8_t bank);
#ifdef USE_ICM20948_MAGNETOMETER
  /// Starts the magnetometer, reached through the bypass to the I2C bus; false leaves it off.
  bool setup_magnetometer_();
  void read_magnetometer_();
  bool magnetometer_ready_{false};
#endif

  ICM20948AccelRange accel_range_{ICM20948_ACCEL_RANGE_4G};
  ICM20948GyroRange gyro_range_{ICM20948_GYRO_RANGE_500};
  uint8_t bank_{0xFF};
};

}  // namespace esphome::icm20948
