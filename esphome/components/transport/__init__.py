import esphome.codegen as cg
import esphome.config_validation as cv

CODEOWNERS = ["@clydebarrow"]
IS_PLATFORM_COMPONENT = True

transport_ns = cg.esphome_ns.namespace("transport")
Transport = transport_ns.class_("Transport", cg.Component)

CONF_TRANSPORT = "transport"

TRANSPORT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Transport),
    }
)


async def register_transport(var, config):
    await cg.register_component(var, config)
