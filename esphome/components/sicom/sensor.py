import esphome.codegen as cg
from esphome.components import resistance_sampler, sensor
from esphome.components.ags10.sensor import CONF_RESISTANCE
from esphome.components.daly_bms.sensor import ICON_CURRENT_DC
from esphome.components.sensor import sensor_ns
from esphome.components.sicom import SICOM_SENSOR_SCHEMA
import esphome.config_validation as cv
from esphome.const import (
    CONF_CURRENT,
    CONF_DEVICE,
    CONF_INDEX,
    CONF_TYPE,
    CONF_VOLTAGE,
    ICON_FLASH,
    UNIT_AMPERE,
    UNIT_OHM,
    UNIT_VOLT,
)

DEPENDENCIES = ["sicom"]
ICON_RESISTOR = "mdi:resistor"
AUTO_LOAD = ["resistance_sampler"]

ResistanceSensor = sensor_ns.class_(
    "Sensor",
    cg.Component,
    sensor.Sensor,
    resistance_sampler.ResistanceSampler,
)


CONFIG_SCHEMA = cv.typed_schema(
    {
        CONF_VOLTAGE: sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT, icon=ICON_FLASH, accuracy_decimals=3
        ).extend(SICOM_SENSOR_SCHEMA),
        CONF_RESISTANCE: sensor.sensor_schema(
            ResistanceSensor,
            unit_of_measurement=UNIT_OHM,
            icon=ICON_RESISTOR,
            accuracy_decimals=0,
        ).extend(SICOM_SENSOR_SCHEMA),
        CONF_CURRENT: sensor.sensor_schema(
            unit_of_measurement=UNIT_AMPERE, icon=ICON_CURRENT_DC, accuracy_decimals=3
        ).extend(SICOM_SENSOR_SCHEMA),
    }
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    paren = await cg.get_variable(config[CONF_DEVICE])
    stype = config[CONF_TYPE]
    cg.add(getattr(paren, f"add_{stype}_sensor")(var, config[CONF_INDEX]))
    return var
