import esphome.codegen as cg
from esphome.components.binary_sensor import binary_sensor_schema, new_binary_sensor
from esphome.components.sicom import SicomDevice
import esphome.config_validation as cv
from esphome.const import CONF_DEBUG, CONF_DEVICE, ENTITY_CATEGORY_CONFIG

CONFIG_SCHEMA = cv.typed_schema(
    {
        CONF_DEBUG: binary_sensor_schema(entity_category=ENTITY_CATEGORY_CONFIG).extend(
            {
                cv.Required(CONF_DEVICE): cv.use_id(SicomDevice),
            }
        )
    }
)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_DEVICE])
    var = new_binary_sensor(config)
    var = cg.new_Pvariable(config[CONF_DEBUG])
    cg.add(paren.add_debug_switch(var))
    return var
