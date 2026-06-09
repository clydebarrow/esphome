import esphome.codegen as cg
from esphome.components import output
import esphome.config_validation as cv
from esphome.const import CONF_CHANNEL, CONF_ID

from . import (
    AXP2101_SUB_SCHEMA,
    CHANNELS,
    CONF_AXP2101_ID,
    VOLTAGE_CHANNELS,
    axp2101_ns,
)

DEPENDENCIES = ["axp2101"]

# Voltage control is only supported for the linearly-encoded rails
VOLTAGE_CHANNEL_OPTIONS = {name: CHANNELS[name] for name in VOLTAGE_CHANNELS}

AXP2101Output = axp2101_ns.class_("AXP2101Output", output.FloatOutput, cg.Parented)

CONFIG_SCHEMA = output.FLOAT_OUTPUT_SCHEMA.extend(AXP2101_SUB_SCHEMA).extend(
    {
        cv.GenerateID(): cv.declare_id(AXP2101Output),
        cv.Required(CONF_CHANNEL): cv.enum(VOLTAGE_CHANNEL_OPTIONS, upper=True),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await output.register_output(var, config)
    await cg.register_parented(var, config[CONF_AXP2101_ID])
    cg.add(var.set_channel(config[CONF_CHANNEL]))
