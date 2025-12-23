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
        read_data: int = 1,
        icon=ICON_SWITCH,
        device_class=cv.UNDEFINED,
        state_class=cv.UNDEFINED,
        entity_category=ENTITY_CATEGORY_CONFIG,
    ):
        self.id = id
        self.msgcode = msgcode
        self.offset = offset
        self.write_code = write_code
        self.read_data = read_data
        self.icon = icon
        self.device_class = device_class
        self.state_class = state_class
        self.entity_category = entity_category

    def get_class(self):
        return SwitchParameter.template(
            self.offset + DATA_OFFSET,
            self.write_code,
            self.read_data,
        )

    def get_name(self):
        return self.id.replace("_", " ").title()

    def get_schema(self):
        def validator(value):
            # Add message code to set for parent to process
            add_message_code(self.msgcode)
            if value is None:
                value = {}
            if CONF_NAME not in value:
                value[CONF_NAME] = self.get_name()

            return cv.maybe_simple_value(
                switch_schema(
                    self.get_class(),
                    default_restore_mode="DISABLED",
                    device_class=self.device_class,
                    entity_category=self.entity_category,
                    icon=self.icon,
                ),
                key=CONF_NAME,
            )(value)

        return validator


SWITCHES = [
    SwitchSetting(
        "backup_supply",
        COMMAND_SETTINGS_DATA,
        12,
        write_code=0x327,
        # off_data=0x20,
        # on_data=0x30,
    ),
    SwitchSetting(
        "shadow_scan",
        COMMAND_SETTINGS_DATA,
        16,
        write_code=0x328,
        # off_data=0x8000,
        # on_data=0x8080,
        read_data=2,
    ),
    SwitchSetting(
        "grid_export_limited",
        COMMAND_SETTINGS_DATA,
        18,
        write_code=0x353,
        # off_data=0x1000,
        # on_data=0x1010,
    ),
]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_CONFIGURE_ALL, default=False): False,
        **{cv.Optional(s.id): s.get_schema() for s in SWITCHES},
        cv.GenerateID(CONF_GOODWE_ID): cv.use_id(Goodwe),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_GOODWE_ID])
    for sensor in SWITCHES:
        if sensor.id in config:
            var = await new_switch(config[sensor.id], sensor.id)
            cg.add(parent.add_setting(sensor.msgcode, var))
