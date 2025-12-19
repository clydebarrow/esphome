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
from esphome.components.number import Number, new_number, number_schema
import esphome.config_validation as cv
from esphome.const import (
    CONF_NAME,
    DEVICE_CLASS_POWER,
    ENTITY_CATEGORY_CONFIG,
    UNIT_WATT,
)

NumberParameter = goodwe_ns.class_("NumberParameter", Parameter, Number)


class NumberSetting:
    def __init__(
        self,
        id,
        msgcode: int,
        offset: int,
        write_code: int,
        min_value: float,
        max_value: float,
        step: float,
        unit_of_measurement: str,
        device_class=cv.UNDEFINED,
        state_class=cv.UNDEFINED,
        entity_category=ENTITY_CATEGORY_CONFIG,
    ):
        self.id = id
        self.msgcode = msgcode
        self.offset = offset
        self.write_code = write_code
        self.min_value = min_value
        self.max_value = max_value
        self.step = step
        self.unit_of_measurement = unit_of_measurement
        self.device_class = device_class
        self.state_class = state_class
        self.entity_category = entity_category

    def get_class(self):
        return NumberParameter.template(self.offset + DATA_OFFSET, self.write_code)

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
                number_schema(
                    self.get_class(),
                    unit_of_measurement=self.unit_of_measurement,
                    device_class=self.device_class,
                    entity_category=self.entity_category,
                ),
                key=CONF_NAME,
            )(value)

        return validator


NUMBERS = [
    NumberSetting(
        "grid_export_limit",
        COMMAND_SETTINGS_DATA,
        52,
        min_value=0,
        max_value=4900,
        step=100,
        write_code=0x335,
        device_class=DEVICE_CLASS_POWER,
        unit_of_measurement=UNIT_WATT,
    ),
]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_CONFIGURE_ALL, default=False): False,
        **{cv.Optional(s.id): s.get_schema() for s in NUMBERS},
        cv.GenerateID(CONF_GOODWE_ID): cv.use_id(Goodwe),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_GOODWE_ID])
    for sensor in NUMBERS:
        if sensor.id in config:
            var = await new_number(
                config[sensor.id],
                sensor.id,
                min_value=sensor.min_value,
                max_value=sensor.max_value,
                step=sensor.step,
            )
            cg.add(parent.add_setting(sensor.msgcode, var))
