import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_CHANNEL

from . import AXP2101_SUB_SCHEMA, CHANNELS, CONF_AXP2101_ID, axp2101_ns

DEPENDENCIES = ["axp2101"]

CONF_CHARGE_ENABLE = "charge_enable"

AXP2101Switch = axp2101_ns.class_("AXP2101Switch", switch.Switch, cg.Parented)

CONFIG_SCHEMA = cv.All(
    switch.switch_schema(AXP2101Switch)
    .extend(AXP2101_SUB_SCHEMA)
    .extend(
        {
            cv.Optional(CONF_CHANNEL): cv.enum(CHANNELS, upper=True),
            cv.Optional(CONF_CHARGE_ENABLE): cv.boolean,
        }
    ),
    cv.has_exactly_one_key(CONF_CHANNEL, CONF_CHARGE_ENABLE),
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_parented(var, config[CONF_AXP2101_ID])
    if CONF_CHARGE_ENABLE in config:
        cg.add(var.set_charge_mode())
    else:
        cg.add(var.set_channel(config[CONF_CHANNEL]))
