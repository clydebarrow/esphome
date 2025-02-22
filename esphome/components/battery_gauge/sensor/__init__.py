import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import CONF_CAPACITY

from .. import tank_gauge_ns

TankGaugeSensor = tank_gauge_ns.class_("TankGaugeSensor", sensor.Sensor, cg.Component)

CONF_CURRENT_SOURCE = "current_source"
CONF_VOLTAGE_SOURCE = "voltage_source"

CONFIG_SCHEMA = (
    sensor.sensor_schema(TankGaugeSensor)
    .extend(
        {
            cv.Required(CONF_VOLTAGE_SOURCE): cv.use_id(sensor.Sensor),
            cv.Required(CONF_CURRENT_SOURCE): cv.use_id(sensor.Sensor),
            cv.Required(CONF_CAPACITY): cv.float_,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)

    voltage_source = await cg.get_variable(config[CONF_VOLTAGE_SOURCE])
    cg.add(var.set_voltage_source(voltage_source))

    current_source = await cg.get_variable(config[CONF_CURRENT_SOURCE])
    cg.add(var.set_current_source(current_source))

    cg.add(var.set_capacity(config[CONF_CAPACITY]))
