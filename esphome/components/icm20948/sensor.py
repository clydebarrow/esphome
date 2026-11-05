from esphome.automation import parent_ref
import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_TEMPERATURE,
    CONF_TYPE,
    DEVICE_CLASS_TEMPERATURE,
    ICON_MAGNET,
    ICON_THERMOMETER,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_MICROTESLA,
)
from esphome.types import ConfigType

from . import (
    CONF_ICM20948_ID,
    MAGNETIC_FIELD,
    MAGNETOMETER_PUBLISHER,
    TEMPERATURE,
    TEMPERATURE_PUBLISHER,
    ICM20948Component,
)

CONF_MAGNETIC_FIELD_X = "magnetic_field_x"
CONF_MAGNETIC_FIELD_Y = "magnetic_field_y"
CONF_MAGNETIC_FIELD_Z = "magnetic_field_z"
_MAGNETIC_FIELDS = (CONF_MAGNETIC_FIELD_X, CONF_MAGNETIC_FIELD_Y, CONF_MAGNETIC_FIELD_Z)

_PARENT_SCHEMA = cv.Schema(
    {cv.GenerateID(CONF_ICM20948_ID): cv.use_id(ICM20948Component)}
)

# `type` defaults to temperature
CONFIG_SCHEMA = cv.typed_schema(
    {
        CONF_TEMPERATURE: sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            icon=ICON_THERMOMETER,
            accuracy_decimals=2,
            state_class=STATE_CLASS_MEASUREMENT,
            device_class=DEVICE_CLASS_TEMPERATURE,
        ).extend(_PARENT_SCHEMA),
        **{
            x: sensor.sensor_schema(
                unit_of_measurement=UNIT_MICROTESLA,
                icon=ICON_MAGNET,
                accuracy_decimals=1,
                state_class=STATE_CLASS_MEASUREMENT,
            ).extend(_PARENT_SCHEMA)
            for x in _MAGNETIC_FIELDS
        },
    },
    default_type=CONF_TEMPERATURE,
)


async def to_code(config: ConfigType) -> None:
    var = await sensor.new_sensor(config)
    parent = await cg.get_variable(config[CONF_ICM20948_ID])
    if config[CONF_TYPE] == CONF_TEMPERATURE:
        TEMPERATURE_PUBLISHER.add(parent, parent_ref(var).publish_state(TEMPERATURE))
        return
    axis = config[CONF_TYPE][-1:]
    MAGNETOMETER_PUBLISHER.add(
        parent, parent_ref(var).publish_state(getattr(MAGNETIC_FIELD, axis))
    )
