import esphome.codegen as cg
from esphome.components import socket, time as time_
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT
from esphome.types import ConfigType

from .. import CONF_SKYECHO_ID, SkyEchoComponent, skyecho_ns

DEPENDENCIES = ["skyecho"]

SkyEchoTime = skyecho_ns.class_("SkyEchoTime", time_.RealTimeClock)

CONFIG_SCHEMA = cv.All(
    time_.TIME_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(SkyEchoTime),
            cv.GenerateID(CONF_SKYECHO_ID): cv.use_id(SkyEchoComponent),
            cv.Optional(CONF_PORT, default=7070): cv.port,
        }
    ),
    socket.consume_sockets(1, "skyecho_time"),
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_PORT])
    await time_.register_time(var, config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_SKYECHO_ID])
    socket.require_tcp_client_link()
