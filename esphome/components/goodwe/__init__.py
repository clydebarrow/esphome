import esphome.codegen as cg
from esphome.components import transport
from esphome.components.transport import CONF_TRANSPORT
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@clydebarrow"]

goodwe_ns = cg.esphome_ns.namespace("goodwe")
Goodwe = goodwe_ns.class_("Goodwe", cg.PollingComponent)

CONFIG_SCHEMA = cv.polling_component_schema("1s").extend(
    {
        cv.GenerateID(): cv.declare_id(Goodwe),
        cv.GenerateID(transport.CONF_TRANSPORT): cv.use_id(transport.Transport),
    }
)


async def to_code(config):
    tvar = await cg.get_variable(config[CONF_TRANSPORT])
    cg.new_Pvariable(config[CONF_ID], tvar)
