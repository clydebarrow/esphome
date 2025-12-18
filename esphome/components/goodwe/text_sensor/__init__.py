import esphome.codegen as cg
from esphome.components.goodwe import (
    COMMAND_SENSOR_DATA,
    COMMAND_SETTINGS_DATA,
    COMMAND_VERSION_DATA,
)
from esphome.components.goodwe.text_sensor.options import WORK_MODES
from esphome.components.text_sensor import new_text_sensor, text_sensor_schema
import esphome.config_validation as cv
from esphome.const import CONF_NAME, DEVICE_CLASS_EMPTY, ENTITY_CATEGORY_DIAGNOSTIC

from .. import (
    CONF_CONFIGURE_ALL,
    CONF_GOODWE_ID,
    DATA_OFFSET,
    Goodwe,
    Parameter,
    goodwe_ns,
)
from .options import (
    BATTERY_MODES,
    GRID_IN_OUT_MODES,
    GRID_MODES,
    INVERTER_MODES,
    LOAD_MODES,
    PV_MODES,
)

TextSensorParameter = goodwe_ns.class_("TextSensorParameter", Parameter)
OptionSensorParameter = goodwe_ns.class_("OptionSensorParameter", Parameter)


class TextSensor:
    def __init__(
        self,
        id,
        msgcode,
        offset,
        length,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        device_class=DEVICE_CLASS_EMPTY,
    ):
        self.id = id
        self.msgcode = msgcode
        self.offset = offset
        self.length = length
        self.entity_category = entity_category
        self.device_class = device_class
        self.options = ()

    def get_class(self):
        return TextSensorParameter.template(self.offset + DATA_OFFSET, self.length)

    def get_name(self):
        return self.id.replace("_", " ").title()

    def get_schema(self):
        return cv.maybe_simple_value(
            text_sensor_schema(
                self.get_class(),
                device_class=self.device_class,
                entity_category=self.entity_category,
            ),
            key=CONF_NAME,
        )


class OptionSensor(TextSensor):
    def __init__(
        self,
        id,
        msgcode,
        offset,
        options,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        device_class=DEVICE_CLASS_EMPTY,
    ):
        super().__init__(
            id,
            msgcode,
            offset,
            1,
            entity_category,
            device_class,
        )
        self.options = options

    def get_class(self):
        return OptionSensorParameter.template(self.offset + DATA_OFFSET)


SENSORS = [
    OptionSensor("pv_mode_1", COMMAND_SENSOR_DATA, 4, PV_MODES),
    OptionSensor("pv_mode_2", COMMAND_SENSOR_DATA, 9, PV_MODES),
    OptionSensor("battery_mode", COMMAND_SENSOR_DATA, 30, BATTERY_MODES),
    OptionSensor("grid_state", COMMAND_SENSOR_DATA, 42, GRID_MODES),
    OptionSensor("load_mode", COMMAND_SENSOR_DATA, 51, LOAD_MODES),
    OptionSensor("inverter_mode", COMMAND_SENSOR_DATA, 51, INVERTER_MODES),
    OptionSensor("grid_mode", COMMAND_SENSOR_DATA, 80, GRID_IN_OUT_MODES),
    OptionSensor("work_mode", COMMAND_SETTINGS_DATA, 67, WORK_MODES),
    TextSensor("firmware_version", COMMAND_VERSION_DATA, 0, 4),
    TextSensor("arm_version", COMMAND_VERSION_DATA, 4, 1),
    TextSensor("model_number", COMMAND_VERSION_DATA, 5, 10),
    TextSensor("serial_number", COMMAND_VERSION_DATA, 31, 16),
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
        if id := config.get(sensor.id):
            var = await new_text_sensor(id, sensor.id)
            for option in sensor.options:
                cg.add(var.add_option(str(option)))
            cg.add(parent.add_parameter(sensor.msgcode, var))
