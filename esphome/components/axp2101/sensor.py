import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_BATTERY_LEVEL,
    CONF_BATTERY_VOLTAGE,
    CONF_TEMPERATURE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
    UNIT_VOLT,
)

from . import AXP2101_SUB_SCHEMA, CONF_AXP2101_ID

DEPENDENCIES = ["axp2101"]

CONF_SYSTEM_VOLTAGE = "system_voltage"
CONF_VBUS_VOLTAGE = "vbus_voltage"


def _voltage_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_VOLT,
        accuracy_decimals=3,
        device_class=DEVICE_CLASS_VOLTAGE,
        state_class=STATE_CLASS_MEASUREMENT,
    )


CONFIG_SCHEMA = AXP2101_SUB_SCHEMA.extend(
    {
        cv.Optional(CONF_BATTERY_VOLTAGE): _voltage_schema(),
        cv.Optional(CONF_VBUS_VOLTAGE): _voltage_schema(),
        cv.Optional(CONF_SYSTEM_VOLTAGE): _voltage_schema(),
        cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    }
)

SETTERS = {
    CONF_BATTERY_VOLTAGE: "set_battery_voltage_sensor",
    CONF_VBUS_VOLTAGE: "set_vbus_voltage_sensor",
    CONF_SYSTEM_VOLTAGE: "set_system_voltage_sensor",
    CONF_BATTERY_LEVEL: "set_battery_level_sensor",
    CONF_TEMPERATURE: "set_temperature_sensor",
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AXP2101_ID])
    for key, setter in SETTERS.items():
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(parent, setter)(sens))
