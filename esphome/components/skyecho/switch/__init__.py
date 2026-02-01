import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv

from .. import CONF_SKYECHO_ID, SkyEchoComponent, skyecho_ns

DEPENDENCIES = ["skyecho"]

SkyEchoSimulateSwitch = skyecho_ns.class_(
    "SkyEchoSimulateSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = switch.switch_schema(SkyEchoSimulateSwitch).extend(
    {
        cv.GenerateID(CONF_SKYECHO_ID): cv.use_id(SkyEchoComponent),
    }
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_SKYECHO_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.set_simulate_switch(var))
