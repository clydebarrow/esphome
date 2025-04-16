import esphome.codegen as cg
from esphome.components import sensor
from esphome.components.mastervolt import (
    CONF_DEVICE_ID,
    MASTERVOLT_SCHEMA,
    MastervoltSensor,
)
import esphome.config_validation as cv
from esphome.config_validation import UNDEFINED
from esphome.const import (
    CONF_BATTERY_LEVEL,
    CONF_BATTERY_VOLTAGE,
    CONF_OFFSET,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY_STORAGE,
    DEVICE_CLASS_VOLTAGE,
    ICON_BATTERY,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_PERCENT,
    UNIT_VOLT,
)

CONF_MESSAGE_ID = "message_id"
CONF_BATTERY_CURRENT = "battery_current"


def sensor_schema(
    message_id,
    offset,
    unit_of_measurement,
    device_class,
    icon=UNDEFINED,
    accuracy_decimals=1,
    state_class=STATE_CLASS_MEASUREMENT,
):
    return MASTERVOLT_SCHEMA.extend(
        sensor.sensor_schema(
            MastervoltSensor,
            unit_of_measurement=unit_of_measurement,
            icon=icon,
            accuracy_decimals=accuracy_decimals,
            state_class=state_class,
            device_class=device_class,
        ),
    ).extend(
        {
            cv.Optional(CONF_MESSAGE_ID, default=message_id): cv.int_range(0, 65535),
            cv.Optional(CONF_OFFSET, default=offset): cv.int_range(0, 15),
        }
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_BATTERY_VOLTAGE): sensor_schema(
            1, 2, unit_of_measurement=UNIT_VOLT, device_class=DEVICE_CLASS_VOLTAGE
        ),
        cv.Optional(CONF_BATTERY_CURRENT): sensor_schema(
            2, 2, unit_of_measurement=UNIT_AMPERE, device_class=DEVICE_CLASS_CURRENT
        ),
        cv.Optional(CONF_BATTERY_LEVEL): sensor_schema(
            0,
            2,
            unit_of_measurement=UNIT_PERCENT,
            device_class=DEVICE_CLASS_ENERGY_STORAGE,
            icon=ICON_BATTERY,
        ),
    }
)


async def to_code(config):
    for key, conf in config.items():
        if key in (CONF_BATTERY_VOLTAGE, CONF_BATTERY_CURRENT, CONF_BATTERY_LEVEL):
            device = await cg.get_variable(conf[CONF_DEVICE_ID])
            var = await sensor.new_sensor(
                conf, conf[CONF_MESSAGE_ID], conf[CONF_OFFSET]
            )
            cg.add(device.add_sensor(var))
