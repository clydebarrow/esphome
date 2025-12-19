import esphome.codegen as cg
from esphome.components.goodwe import (
    COMMAND_SETTINGS_DATA,
    CONF_CONFIGURE_ALL,
    CONF_GOODWE_ID,
    DATA_OFFSET,
    Goodwe,
    Parameter,
    add_message_code,
    goodwe_ns,
)
from esphome.components.switch import Switch, new_switch, switch_schema
import esphome.config_validation as cv
from esphome.const import CONF_NAME, ENTITY_CATEGORY_CONFIG

SwitchParameter = goodwe_ns.class_("SwitchParameter", Parameter, Switch)

ICON_SWITCH = "mdi:toggle-switch"


class SwitchSetting:
    def __init__(
        self,
        id,
        msgcode: int,
        offset: int,
        write_code: int,
        icon=ICON_SWITCH,
        device_class=cv.UNDEFINED,
        state_class=cv.UNDEFINED,
        entity_category=ENTITY_CATEGORY_CONFIG,
    ):
        self.id = id
        self.msgcode = msgcode
        self.offset = offset
        self.write_code = write_code
        self.icon = icon
        self.device_class = device_class
        self.state_class = state_class
        self.entity_category = entity_category

    def get_class(self):
        print(self.offset + DATA_OFFSET, self.write_code)
        return SwitchParameter.template(self.offset + DATA_OFFSET, self.write_code)

    def get_name(self):
        return self.id.replace("_", " ").title()

    def get_schema(self):
        def validator(value):
            # Add message code to set for parent to process
            add_message_code(self.msgcode)
            return cv.maybe_simple_value(
                switch_schema(
                    self.get_class(),
                    device_class=self.device_class,
                    entity_category=self.entity_category,
                    icon=self.icon,
                ),
                key=CONF_NAME,
            )(value)

        return validator


SWITCHES = [
    SwitchSetting("export_limited", COMMAND_SETTINGS_DATA, 19, write_code=0x353),
]

CONFIG_SCHEMA = cv.Any(
    {
        cv.Required(CONF_CONFIGURE_ALL): True,
        **{
            cv.Optional(s.id, default={CONF_NAME: s.get_name()}): s.get_schema()
            for s in SWITCHES
        },
        cv.GenerateID(CONF_GOODWE_ID): cv.use_id(Goodwe),
    },
    {
        cv.Optional(CONF_CONFIGURE_ALL, default=False): False,
        **{cv.Optional(s.id): s.get_schema() for s in SWITCHES},
        cv.GenerateID(CONF_GOODWE_ID): cv.use_id(Goodwe),
    },
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_GOODWE_ID])
    for sensor in SWITCHES:
        if sensor.id in config:
            var = await new_switch(config[sensor.id], sensor.id)
            cg.add(parent.add_setting(sensor.msgcode, var))
