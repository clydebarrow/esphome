import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["network"]

skyecho_ns = cg.esphome_ns.namespace("skyecho")
SkyEchoComponent = skyecho_ns.class_("SkyEcho", cg.PollingComponent)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SkyEchoComponent),
        }
    ).extend(cv.polling_component_schema("1s")),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
