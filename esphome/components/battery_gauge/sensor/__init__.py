import itertools

import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_CALIBRATION,
    CONF_CAPACITY,
    CONF_INITIAL_STATE,
    CONF_VOLTAGE,
    DEVICE_CLASS_ENERGY,
    STATE_CLASS_TOTAL,
    UNIT_PERCENT,
)

from .. import battery_gauge_ns

BatteryGaugeSensor = battery_gauge_ns.class_(
    "BatteryGaugeSensor", sensor.Sensor, cg.Component
)

CONF_CURRENT_SOURCE = "current_source"
CONF_VOLTAGE_SOURCE = "voltage_source"
CONF_PERCENTAGE = "percentage"


def capacity(value):
    if isinstance(value, str) and value.lower().endswith("ah"):
        value = value[:-2]
    return cv.float_(value)


def shorthand(value):
    if isinstance(value, dict) and len(value) == 1:
        voltage = cv.float_(float(list(value.keys())[0]))
        percentage = cv.percentage(list(value.values())[0])
        value = {CONF_VOLTAGE: voltage, CONF_PERCENTAGE: percentage}
    return cv.Schema(
        {
            cv.Required(CONF_VOLTAGE): cv.float_,
            cv.Required(CONF_PERCENTAGE): cv.All(
                cv.percentage_int,
                cv.Range(min=0, max=100),
            ),
        }
    )(value)


CONFIG_SCHEMA = (
    sensor.sensor_schema(
        BatteryGaugeSensor,
        unit_of_measurement=UNIT_PERCENT,
        state_class=STATE_CLASS_TOTAL,
        device_class=DEVICE_CLASS_ENERGY,
        accuracy_decimals=1,
    )
    .extend(
        {
            cv.Required(CONF_VOLTAGE_SOURCE): cv.use_id(sensor.Sensor),
            cv.Required(CONF_CURRENT_SOURCE): cv.use_id(sensor.Sensor),
            cv.Required(CONF_CAPACITY): capacity,
            cv.Required(CONF_CALIBRATION): cv.All(
                cv.ensure_list(shorthand),
                cv.Length(min=2, max=10),
            ),
            cv.Optional(CONF_INITIAL_STATE): cv.All(
                cv.percentage, cv.Range(min=0, max=100)
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    voltage_source = await cg.get_variable(config[CONF_VOLTAGE_SOURCE])
    current_source = await cg.get_variable(config[CONF_CURRENT_SOURCE])
    capacity = config[CONF_CAPACITY]
    charge_map = [
        (x[CONF_VOLTAGE], x[CONF_PERCENTAGE]) for x in config[CONF_CALIBRATION]
    ]
    charge_map.sort(key=lambda x: x[0])
    maps = [list(x[1]) for x in itertools.groupby(charge_map, key=lambda x: x[1] < 0.5)]
    maps[0].sort(key=lambda x: x[0], reverse=True)
    var = await sensor.new_sensor(
        config, voltage_source, current_source, capacity, maps[0], maps[1]
    )
    cg.add(var.set_initial_state(config.get(CONF_INITIAL_STATE)))
    await cg.register_component(var, config)
