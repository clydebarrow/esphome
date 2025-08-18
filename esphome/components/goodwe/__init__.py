import esphome.codegen as cg
from esphome.components import transport
from esphome.components.transport import CONF_TRANSPORT
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.cpp_helpers import register_component

CODEOWNERS = ["@clydebarrow"]

AUTO_LOAD = ["bytebuffer"]

goodwe_ns = cg.esphome_ns.namespace("goodwe")
Goodwe = goodwe_ns.class_("Goodwe", cg.PollingComponent)
Parameter = goodwe_ns.class_("Parameter")

DATA_OFFSET = 7  # Offset for data in the Goodwe protocol (length of header)

CONF_GOODWE_ID = "goodwe_id"

CONFIG_SCHEMA = cv.polling_component_schema("5s").extend(
    {
        cv.GenerateID(): cv.declare_id(Goodwe),
        cv.GenerateID(transport.CONF_TRANSPORT): cv.use_id(transport.Transport),
    }
)


async def to_code(config):
    tvar = await cg.get_variable(config[CONF_TRANSPORT])
    var = cg.new_Pvariable(config[CONF_ID], tvar)
    await register_component(var, config)
