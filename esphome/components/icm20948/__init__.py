import esphome.codegen as cg
from esphome.components import i2c
from esphome.components.motion import MotionComponent

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["i2c", "motion"]
DOMAIN = "icm20948"

CONF_ICM20948_ID = "icm20948_id"
icm20948_ns = cg.esphome_ns.namespace("icm20948")
ICM20948Component = icm20948_ns.class_(
    "ICM20948Component", MotionComponent, i2c.I2CDevice
)
MagneticField = icm20948_ns.struct("MagneticField")

# Each sensor adds its publish of a new reading; a hub without sensors does not read them
TEMPERATURE_PUBLISHER = cg.HubFunction(
    "temperature",
    cg.void,
    [(cg.float_, "temperature")],
    define="USE_ICM20948_TEMPERATURE",
)
TEMPERATURE = TEMPERATURE_PUBLISHER.param("temperature")
MAGNETOMETER_PUBLISHER = cg.HubFunction(
    "magnetometer",
    cg.void,
    [(MagneticField.operator("const").operator("ref"), "field")],
    define="USE_ICM20948_MAGNETOMETER",
)
MAGNETIC_FIELD = MAGNETOMETER_PUBLISHER.param("field")

CONFIG_SCHEMA = {}
