from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.cpp_generator import MockObj, MockObjClass
from esphome.types import ConfigType

CODEOWNERS = ["@clydebarrow"]
DOMAIN = "packet_interface"
IS_PLATFORM_COMPONENT = True

packet_interface_ns = cg.esphome_ns.namespace("packet_interface")
PacketInterface = packet_interface_ns.class_("PacketInterface", cg.Component)

CONF_PACKET_INTERFACE = "packet_interface"
CONF_MAX_PACKET_SIZE = "max_packet_size"


def packet_interface_schema(
    class_: MockObjClass,
    default_max_packet_size: int,
    max_packet_size_limit: int = 4096,
) -> cv.Schema:
    return cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(class_),
            cv.Optional(
                CONF_MAX_PACKET_SIZE, default=default_max_packet_size
            ): cv.int_range(16, max_packet_size_limit),
        }
    ).extend(cv.COMPONENT_SCHEMA)


async def new_packet_interface(config: ConfigType, *args: Any) -> MockObj:
    var = cg.new_Pvariable(config[CONF_ID], *args)
    cg.add(var.set_max_packet_size(config[CONF_MAX_PACKET_SIZE]))
    await cg.register_component(var, config)
    return var
