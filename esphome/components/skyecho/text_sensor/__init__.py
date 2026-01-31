import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.config_validation import polling_component_schema

from .. import CONF_SKYECHO_ID, SkyEchoComponent, skyecho_ns

DEPENDENCIES = ["skyecho"]

SkyEchoTextSensor = skyecho_ns.class_(
    "SkyEchoTextSensor", text_sensor.TextSensor, cg.Component
)

CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema(SkyEchoTextSensor)
    .extend(polling_component_schema("1s"))
    .extend(
        {
            cv.GenerateID(CONF_SKYECHO_ID): cv.use_id(SkyEchoComponent),
        }
    )
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_SKYECHO_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.register_text_sensor(var))
