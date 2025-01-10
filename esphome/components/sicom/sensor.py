import esphome.codegen as cg
from esphome.components import sensor
from esphome.components.ags10.sensor import CONF_RESISTANCE
from esphome.components.sicom import CONF_SICOM_ID, SICOM_SENSOR_SCHEMA
import esphome.config_validation as cv
from esphome.const import CONF_VOLTAGE, ICON_FLASH, UNIT_OHM, UNIT_VOLT

DEPENDENCIES = ["sicom"]
ICON_RESISTOR = "mdi:resistor"

CONFIG_SCHEMA = (
    cv.typed_schema(
        {
            CONF_VOLTAGE: sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT, icon=ICON_FLASH, accuracy_decimals=3
            ),
            CONF_RESISTANCE: sensor.sensor_schema(
                unit_of_measurement=UNIT_OHM, icon=ICON_RESISTOR, accuracy_decimals=0
            ),
        }
    )
    .extend(SICOM_SENSOR_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_SICOM_ID])
    var = await sensor.new_sensor(config)

    cg.add(paren.register_sensor(var))
