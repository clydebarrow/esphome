import esphome.codegen as cg
from esphome.components import binary_sensor
from esphome.components.const import CONF_CONNECTED
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_CONNECTIVITY, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_CHARGER_ID, ChargerComponent

BINARY_SENSORS = [CONF_CONNECTED]

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(CONF_CHARGER_ID): cv.use_id(ChargerComponent),
            cv.Optional(CONF_CONNECTED): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    component = await cg.get_variable(config[CONF_CHARGER_ID])
    for conf_name in BINARY_SENSORS:
        if conf_name in config:
            conf = config[conf_name]
            sensor = await binary_sensor.new_binary_sensor(conf)
            cg.add(component.add_connectivity_sensor(sensor))
