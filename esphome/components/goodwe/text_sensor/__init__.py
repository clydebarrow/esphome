import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_NAME, DEVICE_CLASS_EMPTY, ENTITY_CATEGORY_DIAGNOSTIC

from ...text_sensor import new_text_sensor, text_sensor_schema
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
    LOAD_MODES,
    PV_MODES,
    WORK_MODES,
)

TextSensorParameter = goodwe_ns.class_("TextSensorParameter", Parameter)


class TextSensor:
    def __init__(
        self,
        id,
        msgcode,
        offset,
        options,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        device_class=DEVICE_CLASS_EMPTY,
    ):
        self.id = id
        self.msgcode = msgcode
        self.offset = offset
        self.options = options
        self.entity_category = entity_category
        self.device_class = device_class

    def get_class(self):
        return TextSensorParameter.template(self.offset + DATA_OFFSET)

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


SENSORS = [
    TextSensor("pv_mode_1", 0x106, 4, PV_MODES),
    TextSensor("pv_mode_2", 0x106, 9, PV_MODES),
    TextSensor("battery_mode", 0x106, 30, BATTERY_MODES),
    TextSensor("grid_state", 0x106, 42, GRID_MODES),
    TextSensor("load_mode", 0x106, 51, LOAD_MODES),
    TextSensor("inverter_mode", 0x106, 51, WORK_MODES),
    TextSensor("grid_mode", 0x106, 80, GRID_IN_OUT_MODES),
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
