import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.sicom import SICOM_SENSOR_SCHEMA, SicomComponent, CONF_SICOM_ID
from esphome.components.switch import switch_schema, new_switch
from esphome.const import CONF_DEBUG, ENTITY_CATEGORY_CONFIG, CONF_TYPE

CONF_RELAY = "relay"

CONFIG_SCHEMA = cv.typed_schema(
    {
        CONF_DEBUG: switch_schema(entity_category=ENTITY_CATEGORY_CONFIG).extend(
            {
                cv.GenerateID(CONF_SICOM_ID): cv.declare_id(SicomComponent),
            }
        ),
        CONF_RELAY: switch_schema().extend(SICOM_SENSOR_SCHEMA),
    }
)


async def to_code(config):
    var = await new_switch(config)
    if sicom := await cg.get_variable(config[CONF_SICOM_ID]):
        cg.add(getattr(sicom, f"add_{config[CONF_TYPE]}_switch({var})"))
    return var
