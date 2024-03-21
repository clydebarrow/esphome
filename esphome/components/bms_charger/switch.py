import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_EMPTY,
    ENTITY_CATEGORY_CONFIG,
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


SWITCH_SCHEMA = switch.switch_schema(
    BmsSwitch,
    device_class=DEVICE_CLASS_EMPTY,
    entity_category=ENTITY_CATEGORY_CONFIG,
    default_restore_mode="RESTORE_DEFAULT_OFF",
)

CONFIG_SCHEMA = cv.COMPONENT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_CHARGER_ID): cv.use_id(ChargerComponent),
    }
).extend({cv.Optional(name): SWITCH_SCHEMA for name in SWITCHES})


async def to_code(config):
    component = await cg.get_variable(config[CONF_CHARGER_ID])
    for type in SWITCHES:
        if swconf := config.get(type):
            var = await switch.new_switch(swconf)
            cg.add(component.add_switch(SWITCHES[type], var))
