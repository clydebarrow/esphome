import esphome.codegen as cg
from esphome.components.binary_sensor import (
    BinarySensor,
    binary_sensor_schema,
    new_binary_sensor,
)
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
import esphome.config_validation as cv
from esphome.const import (
    CONF_NAME,
    ENTITY_CATEGORY_NONE,
    ICON_BATTERY,
    STATE_CLASS_MEASUREMENT,
)

BinarySensorParameter = goodwe_ns.class_(
    "BinarySensorParameter", Parameter, BinarySensor
)


class BooleanSensor:
    def __init__(
        self,
        id,
        msgcode,
        offset,
        icon=ICON_BATTERY,
        device_class=cv.UNDEFINED,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_NONE,
    ):
        self.id = id
        self.msgcode = msgcode
        self.offset = offset
        self.icon = icon
        self.device_class = device_class
        self.state_class = state_class
        self.entity_category = entity_category

    def get_class(self):
        return BinarySensorParameter.template(self.offset + DATA_OFFSET)

    def get_name(self):
        return self.id.replace("_", " ").title()

    def get_schema(self):
        def validator(value):
            # Add message code to set for parent to process
            add_message_code(self.msgcode)
            return cv.maybe_simple_value(
                binary_sensor_schema(
                    self.get_class(),
                    device_class=self.device_class,
                    entity_category=self.entity_category,
                    icon=self.icon,
                ),
                key=CONF_NAME,
            )(value)

        return validator


SENSORS = [
    BooleanSensor("backup_supply", COMMAND_SETTINGS_DATA, 12),
    BooleanSensor("off-grid_charge", COMMAND_SETTINGS_DATA, 14),
    BooleanSensor("shadow_scan", COMMAND_SETTINGS_DATA, 16),
    BooleanSensor("battery_activated", COMMAND_SETTINGS_DATA, 34),
    BooleanSensor("bp_off_grid_charge", COMMAND_SETTINGS_DATA, 36),
    BooleanSensor("bp_pv_discharge", COMMAND_SETTINGS_DATA, 38),
    BooleanSensor("export_limited", COMMAND_SETTINGS_DATA, 18),
]

CONFIG_SCHEMA = cv.Any(
    {
        cv.Required(CONF_CONFIGURE_ALL): True,
        **{
            cv.Optional(s.id, default={CONF_NAME: s.get_name()}): s.get_schema()
            for s in SENSORS
        },
        cv.GenerateID(CONF_GOODWE_ID): cv.use_id(Goodwe),
    },
    {
        cv.Optional(CONF_CONFIGURE_ALL, default=False): False,
        **{cv.Optional(s.id): s.get_schema() for s in SENSORS},
        cv.GenerateID(CONF_GOODWE_ID): cv.use_id(Goodwe),
    },
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_GOODWE_ID])
    for sensor in SENSORS:
        if sensor.id in config:
            var = await new_binary_sensor(config[sensor.id], sensor.id)
            cg.add(parent.add_parameter(sensor.msgcode, var))
