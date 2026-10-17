from typing import Any

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_BINARY_SENSOR,
    CONF_ID,
    CONF_TIMEOUT,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from esphome.cpp_generator import MockObj, MockObjClass
from esphome.types import ConfigType

AUTO_LOAD = ["binary_sensor"]
CODEOWNERS = ["@clydebarrow"]
DOMAIN = "packet_interface"
IS_PLATFORM_COMPONENT = True

packet_interface_ns = cg.esphome_ns.namespace("packet_interface")
PacketInterface = packet_interface_ns.class_("PacketInterface", cg.Component)
HdlcReliability = packet_interface_ns.class_("HdlcReliability", PacketInterface)
BasicSession = packet_interface_ns.class_("BasicSession", PacketInterface)

CONF_KEEP_ALIVE = "keep_alive"
CONF_PACKET_INTERFACE = "packet_interface"
CONF_MAX_PACKET_SIZE = "max_packet_size"
CONF_MAX_QUEUE_LENGTH = "max_queue_length"
CONF_MAX_RETRIES = "max_retries"
CONF_RELIABILITY = "reliability"
CONF_SESSION = "session"

RELIABILITY_HDLC = "hdlc"
SESSION_BASIC = "basic"


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


def _session_timeout(config: ConfigType) -> ConfigType:
    """Default the timeout to three keep-alive intervals, and require it to be longer than one."""
    keep_alive = config[CONF_KEEP_ALIVE].total_milliseconds
    if (timeout := config.get(CONF_TIMEOUT)) is None:
        config[CONF_TIMEOUT] = cv.positive_time_period_milliseconds(
            f"{keep_alive * 3}ms"
        )
    elif timeout.total_milliseconds <= keep_alive:
        raise cv.Invalid("Must be longer than keep_alive", path=[CONF_TIMEOUT])
    return config


SESSION_SCHEMA = cv.All(
    _empty_dict,
    cv.typed_schema(
        {
            SESSION_BASIC: cv.All(
                cv.Schema(
                    {
                        cv.GenerateID(): cv.declare_id(BasicSession),
                        cv.Optional(CONF_KEEP_ALIVE, default="10s"): cv.All(
                            cv.positive_time_period_milliseconds,
                            cv.Range(min=cv.TimePeriod(seconds=1)),
                        ),
                        cv.Optional(CONF_TIMEOUT): cv.positive_time_period_milliseconds,
                        cv.Optional(
                            CONF_BINARY_SENSOR
                        ): binary_sensor.binary_sensor_schema(
                            device_class=DEVICE_CLASS_CONNECTIVITY,
                            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                        ),
                    }
                ).extend(cv.COMPONENT_SCHEMA),
                _session_timeout,
            ),
        },
        default_type=SESSION_BASIC,
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
            cv.Optional(CONF_SESSION): SESSION_SCHEMA,
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


async def _new_session(config: ConfigType, lower: MockObj) -> MockObj:
    cg.add_define("USE_PACKET_INTERFACE_BASIC_SESSION")
    var = cg.new_Pvariable(config[CONF_ID], lower)
    cg.add(var.set_keep_alive(config[CONF_KEEP_ALIVE]))
    cg.add(var.set_session_timeout(config[CONF_TIMEOUT]))
    await cg.register_component(var, config)
    binary_sensors = binary_sensor.sub_binary_sensors(config)
    await binary_sensors(CONF_BINARY_SENSOR, var.set_connected_sensor)
    return var


async def new_packet_interface(config: ConfigType, *args: Any) -> MockObj:
    """Create the link layer and the layers configured on it, from the bottom up.

    Returns the link layer; applications reach the top layer with PacketInterface::top().
    """
    var = cg.new_Pvariable(config[CONF_ID], *args)
    cg.add(var.set_name(config[CONF_ID].id))
    cg.add(var.set_max_packet_size(config[CONF_MAX_PACKET_SIZE]))
    await cg.register_component(var, config)
    top = var
    if (reliability := config.get(CONF_RELIABILITY)) is not None:
        top = await _new_reliability(reliability, top)
    if (session := config.get(CONF_SESSION)) is not None:
        await _new_session(session, top)
    return var
