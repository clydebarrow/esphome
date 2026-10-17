"""Reliability layer using HDLC style acknowledgement and go-back-N retransmission."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIMEOUT
from esphome.cpp_generator import MockObj
from esphome.types import ConfigType

from . import RELIABILITY, PacketInterface, packet_interface_ns

CONF_MAX_QUEUE_LENGTH = "max_queue_length"
CONF_MAX_RETRIES = "max_retries"

HdlcReliability = packet_interface_ns.class_("HdlcReliability", PacketInterface)

HDLC_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HdlcReliability),
        cv.Optional(
            CONF_TIMEOUT, default="200ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_MAX_QUEUE_LENGTH, default=4): cv.int_range(1, 7),
        cv.Optional(CONF_MAX_RETRIES, default=5): cv.int_range(1, 255),
    }
).extend(cv.COMPONENT_SCHEMA)


@RELIABILITY.register("hdlc", schema=HDLC_SCHEMA, single_peer=lambda config: True)
async def build_hdlc(config: ConfigType, lower: MockObj) -> MockObj:
    cg.add_define("USE_PACKET_INTERFACE_HDLC")
    var = cg.new_Pvariable(config[CONF_ID], lower)
    cg.add(var.set_ack_timeout(config[CONF_TIMEOUT]))
    cg.add(var.set_max_queue_length(config[CONF_MAX_QUEUE_LENGTH]))
    cg.add(var.set_max_retries(config[CONF_MAX_RETRIES]))
    await cg.register_component(var, config)
    return var
