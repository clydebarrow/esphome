import esphome.codegen as cg
from esphome.components import i2c
from esphome.components.const import CONF_ACCELEROMETER_RANGE, CONF_GYROSCOPE_RANGE
from esphome.components.motion import motion_schema, new_motion_component
import esphome.config_validation as cv
from esphome.types import ConfigType

from . import ICM20948Component, icm20948_ns

#  Enum proxies (must match the C++ enum values exactly)
ICM20948AccelRange = icm20948_ns.enum("ICM20948AccelRange")
ACCEL_RANGE_OPTIONS = ICM20948AccelRange.options(
    "2G", "4G", "8G", "16G", prefix="ICM20948_ACCEL_RANGE_"
)

ICM20948GyroRange = icm20948_ns.enum("ICM20948GyroRange")
GYRO_RANGE_OPTIONS = {
    "250DPS": ICM20948GyroRange.ICM20948_GYRO_RANGE_250,
    "500DPS": ICM20948GyroRange.ICM20948_GYRO_RANGE_500,
    "1000DPS": ICM20948GyroRange.ICM20948_GYRO_RANGE_1000,
    "2000DPS": ICM20948GyroRange.ICM20948_GYRO_RANGE_2000,
}

CONFIG_SCHEMA = (
    motion_schema(ICM20948Component, has_accel=True, has_gyro=True)
    .extend(
        {
            cv.Optional(CONF_ACCELEROMETER_RANGE, default="4G"): cv.enum(
                ACCEL_RANGE_OPTIONS, upper=True
            ),
            cv.Optional(CONF_GYROSCOPE_RANGE, default="500DPS"): cv.enum(
                GYRO_RANGE_OPTIONS, upper=True
            ),
        }
    )
    .extend(i2c.i2c_device_schema(0x68))
)


async def to_code(config: ConfigType) -> None:
    var = await new_motion_component(config)
    await i2c.register_i2c_device(var, config)
    cg.add(var.set_accel_range(config[CONF_ACCELEROMETER_RANGE]))
    cg.add(var.set_gyro_range(config[CONF_GYROSCOPE_RANGE]))
