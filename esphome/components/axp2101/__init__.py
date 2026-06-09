import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["i2c"]
MULTI_CONF = True

CONF_AXP2101_ID = "axp2101_id"

axp2101_ns = cg.esphome_ns.namespace("axp2101")
AXP2101 = axp2101_ns.class_("AXP2101", cg.PollingComponent, i2c.I2CDevice)

# Power rails. Order MUST match the Channel enum in axp2101.h
Channel = axp2101_ns.enum("Channel", is_class=True)
CHANNELS = {
    "DCDC1": Channel.DCDC1,
    "DCDC2": Channel.DCDC2,
    "DCDC3": Channel.DCDC3,
    "DCDC4": Channel.DCDC4,
    "DCDC5": Channel.DCDC5,
    "ALDO1": Channel.ALDO1,
    "ALDO2": Channel.ALDO2,
    "ALDO3": Channel.ALDO3,
    "ALDO4": Channel.ALDO4,
    "BLDO1": Channel.BLDO1,
    "BLDO2": Channel.BLDO2,
    "DLDO1": Channel.DLDO1,
    "DLDO2": Channel.DLDO2,
}

# Rails whose voltage is encoded as a simple linear value and can be driven by the
# `output` platform. The piecewise-encoded DCDC2/3/4 rails support enable/disable only.
VOLTAGE_CHANNELS = [
    name for name in CHANNELS if name not in ("DCDC2", "DCDC3", "DCDC4")
]

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AXP2101),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(i2c.i2c_device_schema(0x34))
)

# Schema fragment for the sensor/binary_sensor/switch/output sub-platforms
AXP2101_SUB_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_AXP2101_ID): cv.use_id(AXP2101),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
