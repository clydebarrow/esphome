from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIMEOUT
from esphome.cpp_generator import MockObj, MockObjClass
from esphome.types import ConfigType

CODEOWNERS = ["@clydebarrow"]
DOMAIN = "packet_interface"
IS_PLATFORM_COMPONENT = True

packet_interface_ns = cg.esphome_ns.namespace("packet_interface")
PacketInterface = packet_interface_ns.class_("PacketInterface", cg.Component)
HdlcReliability = packet_interface_ns.class_("HdlcReliability", PacketInterface)

CONF_PACKET_INTERFACE = "packet_interface"
CONF_MAX_PACKET_SIZE = "max_packet_size"
CONF_MAX_QUEUE_LENGTH = "max_queue_length"
CONF_MAX_RETRIES = "max_retries"
CONF_RELIABILITY = "reliability"

RELIABILITY_HDLC = "hdlc"


def _empty_dict(value: Any) -> Any:
    """Allow a layer to be enabled with no options, e.g. `reliability:`."""
    return {} if value is None else value


RELIABILITY_SCHEMA = cv.All(
    _empty_dict,
    cv.typed_schema(
        {
            RELIABILITY_HDLC: cv.Schema(
                {
                    cv.GenerateID(): cv.declare_id(HdlcReliability),
                    cv.Optional(
                        CONF_TIMEOUT, default="200ms"
                    ): cv.positive_time_period_milliseconds,
                    cv.Optional(CONF_MAX_QUEUE_LENGTH, default=4): cv.int_range(1, 7),
                    cv.Optional(CONF_MAX_RETRIES, default=5): cv.int_range(1, 255),
                }
            ).extend(cv.COMPONENT_SCHEMA),
        },
        default_type=RELIABILITY_HDLC,
    ),
)


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
            cv.Optional(CONF_RELIABILITY): RELIABILITY_SCHEMA,
        }
    ).extend(cv.COMPONENT_SCHEMA)


async def _new_reliability(config: ConfigType, lower: MockObj) -> MockObj:
    cg.add_define("USE_PACKET_INTERFACE_HDLC")
    var = cg.new_Pvariable(config[CONF_ID], lower)
    cg.add(var.set_ack_timeout(config[CONF_TIMEOUT]))
    cg.add(var.set_max_queue_length(config[CONF_MAX_QUEUE_LENGTH]))
    cg.add(var.set_max_retries(config[CONF_MAX_RETRIES]))
    await cg.register_component(var, config)
    return var


async def new_packet_interface(config: ConfigType, *args: Any) -> MockObj:
    """Create the link layer and the layers configured on it, from the bottom up.

    Returns the link layer; applications reach the top layer with PacketInterface::top().
    """
    var = cg.new_Pvariable(config[CONF_ID], *args)
    cg.add(var.set_name(config[CONF_ID].id))
    cg.add(var.set_max_packet_size(config[CONF_MAX_PACKET_SIZE]))
    await cg.register_component(var, config)
    if (reliability := config.get(CONF_RELIABILITY)) is not None:
        await _new_reliability(reliability, var)
    return var
