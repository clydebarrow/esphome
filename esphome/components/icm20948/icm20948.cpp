#include "icm20948.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::icm20948 {

ESPHOME_LOG_TAG(TAG, "icm20948");

// The registers are in four banks, chosen with REG_BANK_SEL (present in every bank)
static constexpr uint8_t REG_BANK_SEL = 0x7F;

// Bank 0
static constexpr uint8_t REG_WHO_AM_I = 0x00;
static constexpr uint8_t REG_PWR_MGMT_1 = 0x06;
static constexpr uint8_t REG_USER_CTRL = 0x03;
static constexpr uint8_t REG_PWR_MGMT_2 = 0x07;
static constexpr uint8_t REG_INT_PIN_CFG = 0x0F;
static constexpr uint8_t REG_ACCEL_XOUT_H = 0x2D;

// Bank 2
static constexpr uint8_t REG_GYRO_SMPLRT_DIV = 0x00;
static constexpr uint8_t REG_GYRO_CONFIG_1 = 0x01;
static constexpr uint8_t REG_ACCEL_SMPLRT_DIV_2 = 0x11;
static constexpr uint8_t REG_ACCEL_CONFIG = 0x14;

static constexpr uint8_t WHO_AM_I_VALUE = 0xEA;
static constexpr uint8_t PWR_MGMT_1_RESET = 0x80;
// Awake, with the best available clock
static constexpr uint8_t PWR_MGMT_1_CLKSEL_AUTO = 0x01;
// Low-pass filter on (FCHOICE), its configuration 3: about 50 Hz for both sensors
static constexpr uint8_t CONFIG_DLPF = (3 << 3) | 0x01;
// About 100 Hz output from the 1.1 kHz internal rate
static constexpr uint8_t SAMPLE_RATE_DIV = 10;
// INT_PIN_CFG BYPASS_EN: the auxiliary I2C pins, with the AK09916, join the host's bus
static constexpr uint8_t INT_PIN_CFG_BYPASS = 0x02;

// The AK09916 magnetometer
static constexpr uint8_t AK09916_ADDRESS = 0x0C;
static constexpr uint8_t AK09916_REG_WIA2 = 0x01;
static constexpr uint8_t AK09916_REG_ST1 = 0x10;
static constexpr uint8_t AK09916_REG_CNTL2 = 0x31;
static constexpr uint8_t AK09916_REG_CNTL3 = 0x32;
static constexpr uint8_t AK09916_WIA2_VALUE = 0x09;
static constexpr uint8_t AK09916_CONTINUOUS_100HZ = 0x08;
static constexpr uint8_t AK09916_SOFT_RESET = 0x01;
// ST1, the three axes (little-endian), a reserved byte and ST2: reading ST2 ends the measurement
static constexpr size_t AK09916_DATA_SIZE = 9;
static constexpr uint8_t AK09916_ST2_HOFL = 0x08;
static constexpr float AK09916_SCALE = 0.15f;  // µT per count

// Datasheet: degrees C = raw / 333.87 + 21
static constexpr float TEMPERATURE_SENSITIVITY = 333.87f;
static constexpr float TEMPERATURE_OFFSET = 21.0f;

bool ICM20948Component::select_bank_(uint8_t bank) {
  if (bank == this->bank_)
    return true;
  if (!this->write_byte(REG_BANK_SEL, bank << 4))
    return false;
  this->bank_ = bank;
  return true;
}

void ICM20948Component::setup() {
  MotionComponent::setup();

  uint8_t who_am_i = 0;
  if (!this->setup_check(this->select_bank_(0) && this->read_byte(REG_WHO_AM_I, &who_am_i)))
    return;
  if (who_am_i != WHO_AM_I_VALUE) {
    ESP_LOGE(TAG, "Unexpected WHO_AM_I 0x%02X, expected 0x%02X", who_am_i, WHO_AM_I_VALUE);
    this->mark_failed(LOG_STR("Wrong chip ID"));
    return;
  }

  // A reset returns every register, the bank selection included, to its default, and leaves the chip asleep
  this->write_byte(REG_PWR_MGMT_1, PWR_MGMT_1_RESET);
  this->bank_ = 0;
  delay(100);  // NOLINT
  bool ok = this->write_byte(REG_PWR_MGMT_1, PWR_MGMT_1_CLKSEL_AUTO);
  delay(10);                                          // NOLINT
  ok = ok && this->write_byte(REG_PWR_MGMT_2, 0x00);  // all axes on

  ok = ok && this->select_bank_(2);
  ok = ok && this->write_byte(REG_GYRO_SMPLRT_DIV, SAMPLE_RATE_DIV);
  ok = ok && this->write_byte(REG_GYRO_CONFIG_1, CONFIG_DLPF | (this->gyro_range_ << 1));
  ok = ok && this->write_byte(REG_ACCEL_SMPLRT_DIV_2, SAMPLE_RATE_DIV);
  ok = ok && this->write_byte(REG_ACCEL_CONFIG, CONFIG_DLPF | (this->accel_range_ << 1));
  ok = ok && this->select_bank_(0);
  if (!this->setup_check(ok))
    return;
#ifdef USE_ICM20948_MAGNETOMETER
  this->magnetometer_ready_ = this->magnetometer_publisher_ != nullptr && this->setup_magnetometer_();
#endif
}

#ifdef USE_ICM20948_MAGNETOMETER
bool ICM20948Component::setup_magnetometer_() {
  // The ICM-20948's own I2C master is off after the reset, so the bypass can be opened
  if (!this->write_byte(REG_USER_CTRL, 0x00) || !this->write_byte(REG_INT_PIN_CFG, INT_PIN_CFG_BYPASS)) {
    ESP_LOGW(TAG, "Could not open the bypass to the magnetometer");
    return false;
  }
  uint8_t reg = AK09916_REG_WIA2;
  uint8_t id = 0;
  if (this->bus_->write_readv(AK09916_ADDRESS, &reg, 1, &id, 1) != i2c::ERROR_OK || id != AK09916_WIA2_VALUE) {
    ESP_LOGW(TAG, "No AK09916 magnetometer at 0x%02X (ID 0x%02X)", AK09916_ADDRESS, id);
    return false;
  }
  const uint8_t reset[2] = {AK09916_REG_CNTL3, AK09916_SOFT_RESET};
  this->bus_->write(AK09916_ADDRESS, reset, sizeof(reset));
  delay(1);  // NOLINT
  const uint8_t mode[2] = {AK09916_REG_CNTL2, AK09916_CONTINUOUS_100HZ};
  if (this->bus_->write(AK09916_ADDRESS, mode, sizeof(mode)) != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "Could not start the magnetometer");
    return false;
  }
  return true;
}

void ICM20948Component::read_magnetometer_() {
  uint8_t reg = AK09916_REG_ST1;
  uint8_t raw[AK09916_DATA_SIZE];
  if (this->bus_->write_readv(AK09916_ADDRESS, &reg, 1, raw, sizeof(raw)) != i2c::ERROR_OK)
    return;
  // Bit 0 of ST1: a new measurement is ready
  if ((raw[0] & 0x01) == 0)
    return;
  const bool overflow = (raw[8] & AK09916_ST2_HOFL) != 0;
  // The AK09916's Y and Z axes point the other way from the accelerometer's
  const MagneticField field{
      overflow ? NAN : encode_value_le<int16_t>(&raw[1]) * AK09916_SCALE,
      overflow ? NAN : -encode_value_le<int16_t>(&raw[3]) * AK09916_SCALE,
      overflow ? NAN : -encode_value_le<int16_t>(&raw[5]) * AK09916_SCALE,
  };
  this->magnetometer_publisher_(field);
}
#endif

void ICM20948Component::dump_config() {
  ESP_LOGCONFIG(TAG,
                "ICM-20948:\n"
                "  Accelerometer range: ±%ug\n"
                "  Gyroscope range: ±%u°/s",
                2u << this->accel_range_, 250u << this->gyro_range_);
  LOG_I2C_DEVICE(this);
#ifdef USE_ICM20948_MAGNETOMETER
  ESP_LOGCONFIG(TAG, "  Magnetometer: %s",
                this->magnetometer_ready_ ? LOG_STR_LITERAL("AK09916") : LOG_STR_LITERAL("none"));
#endif
  MotionComponent::dump_config();
}

bool ICM20948Component::update_data(motion::MotionData &data) {
  // Accelerometer X, Y, Z, gyroscope X, Y, Z, temperature: big-endian 16-bit values
  uint8_t raw[14];
  if (!this->status_comm_result(this->select_bank_(0) && this->read_bytes(REG_ACCEL_XOUT_H, raw, sizeof(raw)))) {
    // The bank is unknown after a failed write
    this->bank_ = 0xFF;
    return false;
  }

  const float accel_scale = static_cast<float>(2u << this->accel_range_) / 32768.0f;
  const float gyro_scale = static_cast<float>(250u << this->gyro_range_) / 32768.0f;
  for (uint8_t axis = 0; axis != 3; axis++) {
    data.acceleration[axis] = encode_value<int16_t>(&raw[axis * 2]) * accel_scale;
    data.angular_rate[axis] = encode_value<int16_t>(&raw[6 + axis * 2]) * gyro_scale;
  }
#ifdef USE_ICM20948_TEMPERATURE
  if (this->temperature_publisher_ != nullptr)
    this->temperature_publisher_(encode_value<int16_t>(&raw[12]) / TEMPERATURE_SENSITIVITY + TEMPERATURE_OFFSET);
#endif
#ifdef USE_ICM20948_MAGNETOMETER
  if (this->magnetometer_ready_)
    this->read_magnetometer_();
#endif
  return true;
}

}  // namespace esphome::icm20948
