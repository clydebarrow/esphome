import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_TYPE,
)
from esphome.components import switch

from . import (
    charger,
    ChargerComponent,
    CONF_CHARGER_ID,
)

BmsSwitch = charger.class_("BmsSwitch", switch.Switch, cg.Component)
SwitchType = charger.enum("SwitchType")


SWITCHES = {
    "full_charge": SwitchType.SW_FULL_CHARGE,
    "force_charge": SwitchType.SW_FORCE_CHARGE_1,
    "no_discharge": SwitchType.SW_NO_DISCHARGE,
    "no_charge": SwitchType.SW_NO_CHARGE,
    "suppress_ovp": SwitchType.SW_SUPPRESS_OVP,
}


def switch_schema(name):
    return (
        cv.Optional(name),
        cv.Required(CONF_TYPE),
        switch.switch_schema(BmsSwitch),
    )


CONFIG_SCHEMA = (
    switch.switch_schema(BmsSwitch)
    .extend(
        {
            cv.Required(CONF_TYPE): cv.enum(SWITCHES),
            cv.GenerateID(CONF_CHARGER_ID): cv.use_id(ChargerComponent),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    component = await cg.get_variable(config[CONF_CHARGER_ID])
    type = config[CONF_TYPE]
    if type in SWITCHES:
        var = await switch.new_switch(config)
        cg.add(component.add_switch(type, var))
