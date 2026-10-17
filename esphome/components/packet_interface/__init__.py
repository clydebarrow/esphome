from collections.abc import Callable
import hashlib
from typing import Any

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_BINARY_SENSOR,
    CONF_ENCRYPTION,
    CONF_ID,
    CONF_KEY,
    CONF_TIMEOUT,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from esphome.core import ID
from esphome.cpp_generator import MockObj, MockObjClass
import esphome.final_validate as fv
from esphome.types import ConfigType

AUTO_LOAD = ["binary_sensor", "xxtea"]
CODEOWNERS = ["@clydebarrow"]
DOMAIN = "packet_interface"
IS_PLATFORM_COMPONENT = True

packet_interface_ns = cg.esphome_ns.namespace("packet_interface")
PacketInterface = packet_interface_ns.class_("PacketInterface", cg.Component)
HdlcReliability = packet_interface_ns.class_("HdlcReliability", PacketInterface)
BasicSession = packet_interface_ns.class_("BasicSession", PacketInterface)
XxteaEncryption = packet_interface_ns.class_("XxteaEncryption", PacketInterface)
BasicAddressing = packet_interface_ns.class_("BasicAddressing", cg.Component)
AddressingPort = packet_interface_ns.class_("AddressingPort", PacketInterface)

CONF_ADDRESSING = "addressing"
CONF_DESTINATION = "destination"
CONF_KEEP_ALIVE = "keep_alive"
CONF_PACKET_INTERFACE = "packet_interface"
CONF_PEERS = "peers"
CONF_PORT_ID = "port_id"
CONF_MAX_PACKET_SIZE = "max_packet_size"
CONF_MAX_QUEUE_LENGTH = "max_queue_length"
CONF_MAX_RETRIES = "max_retries"
CONF_RELIABILITY = "reliability"
CONF_ROLLING_CODE_ENABLE = "rolling_code_enable"
CONF_SESSION = "session"

ADDRESSING_BASIC = "basic"
BROADCAST_ADDRESS = 0xFFFF
ENCRYPTION_XXTEA = "xxtea"
RELIABILITY_HDLC = "hdlc"
SESSION_BASIC = "basic"

_request_port_slot = cg.slot_counter("PACKET_INTERFACE_ADDRESSING_PORT_COUNT")


def _empty_dict(value: Any) -> Any:
    """Allow a layer to be enabled with no options, e.g. `reliability:`."""
    return {} if value is None else value


def _key_only(value: Any) -> Any:
    """Allow the encryption key to be given on its own, e.g. `encryption: my secret`."""
    return {CONF_KEY: value} if isinstance(value, str) else value


ENCRYPTION_SCHEMA = cv.All(
    _key_only,
    cv.typed_schema(
        {
            ENCRYPTION_XXTEA: cv.Schema(
                {
                    cv.GenerateID(): cv.declare_id(XxteaEncryption),
                    cv.Required(CONF_KEY): cv.All(
                        cv.sensitive(cv.string), cv.Length(min=1)
                    ),
                    cv.Optional(CONF_ROLLING_CODE_ENABLE, default=False): cv.boolean,
                }
            ).extend(cv.COMPONENT_SCHEMA),
        },
        default_type=ENCRYPTION_XXTEA,
    ),
)


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


# The layers that can be stacked above a link, or above each peer of an addressing layer
LAYER_SCHEMAS = {
    cv.Optional(CONF_ENCRYPTION): ENCRYPTION_SCHEMA,
    cv.Optional(CONF_RELIABILITY): RELIABILITY_SCHEMA,
    cv.Optional(CONF_SESSION): SESSION_SCHEMA,
}


def _destination(value: Any) -> int:
    """A device address, or broadcast."""
    if isinstance(value, str) and value.lower() == "broadcast":
        return BROADCAST_ADDRESS
    return cv.hex_int_range(1, BROADCAST_ADDRESS)(value)


def _check_two_device_layers(config: ConfigType, destination: int) -> None:
    """Layers that keep state for one peer need a single destination on a shared channel."""
    if destination != BROADCAST_ADDRESS:
        return
    for key in (CONF_RELIABILITY, CONF_SESSION):
        if key in config:
            raise cv.Invalid(
                f"'{key}' works between two devices only, so it needs a '{CONF_DESTINATION}' other than broadcast",
                path=[key],
            )
    if config.get(CONF_ENCRYPTION, {}).get(CONF_ROLLING_CODE_ENABLE):
        raise cv.Invalid(
            f"The rolling code works between two devices only, so it needs a '{CONF_DESTINATION}' other than broadcast",
            path=[CONF_ENCRYPTION, CONF_ROLLING_CODE_ENABLE],
        )


def _validate_peer(config: ConfigType) -> ConfigType:
    _check_two_device_layers(config, config[CONF_DESTINATION])
    return config


def _unique_destinations(peers: list[ConfigType]) -> list[ConfigType]:
    destinations = [peer[CONF_DESTINATION] for peer in peers]
    if len(set(destinations)) != len(destinations):
        raise cv.Invalid("Each peer must have a different destination")
    return peers


PEER_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.declare_id(AddressingPort),
            cv.Required(CONF_DESTINATION): _destination,
            **LAYER_SCHEMAS,
        }
    ),
    _validate_peer,
)

ADDRESSING_SCHEMA = cv.typed_schema(
    {
        ADDRESSING_BASIC: cv.All(
            cv.Schema(
                {
                    cv.GenerateID(): cv.declare_id(BasicAddressing),
                    cv.GenerateID(CONF_PORT_ID): cv.declare_id(AddressingPort),
                    cv.Required(CONF_ADDRESS): cv.hex_int_range(
                        1, BROADCAST_ADDRESS - 1
                    ),
                    cv.Optional(CONF_DESTINATION): _destination,
                    cv.Optional(CONF_PEERS): cv.All(
                        cv.ensure_list(PEER_SCHEMA),
                        cv.Length(min=1),
                        _unique_destinations,
                    ),
                }
            ).extend(cv.COMPONENT_SCHEMA),
            cv.has_at_most_one_key(CONF_DESTINATION, CONF_PEERS),
        ),
    },
    default_type=ADDRESSING_BASIC,
)


def _validate_layers(config: ConfigType) -> ConfigType:
    """With peers, the layers go in each peer; otherwise check them against the one destination."""
    if (addressing := config.get(CONF_ADDRESSING)) is None:
        return config
    if CONF_PEERS in addressing:
        for key in (CONF_ENCRYPTION, CONF_RELIABILITY, CONF_SESSION):
            if key in config:
                raise cv.Invalid(
                    f"With '{CONF_PEERS}', '{key}' goes in each peer", path=[key]
                )
        return config
    addressing.setdefault(CONF_DESTINATION, BROADCAST_ADDRESS)
    _check_two_device_layers(config, addressing[CONF_DESTINATION])
    return config


def final_validate_interface(key: str) -> Callable[[ConfigType], ConfigType]:
    """Final validation for an application: a link with several peers must be used through one of the peers."""

    def validator(config: ConfigType) -> ConfigType:
        interface_id: ID = config[key]
        for interface in fv.full_config.get().get(DOMAIN, []):
            if interface[CONF_ID].id == interface_id.id and CONF_PEERS in interface.get(
                CONF_ADDRESSING, {}
            ):
                raise cv.Invalid(
                    f"'{interface_id.id}' has several peers, so use the id of one of them",
                    path=[key],
                )
        return config

    return validator


def packet_interface_schema(
    class_: MockObjClass,
    default_max_packet_size: int,
    max_packet_size_limit: int = 4096,
) -> cv.Schema:
    return (
        cv.Schema(
            {
                cv.GenerateID(): cv.declare_id(class_),
                cv.Optional(
                    CONF_MAX_PACKET_SIZE, default=default_max_packet_size
                ): cv.int_range(16, max_packet_size_limit),
                cv.Optional(CONF_ADDRESSING): ADDRESSING_SCHEMA,
                **LAYER_SCHEMAS,
            }
        )
        .extend(cv.COMPONENT_SCHEMA)
        .add_extra(_validate_layers)
    )


async def _new_port(
    port_id: ID, addressing: MockObj, destination: int, only_port: bool
) -> MockObj:
    _request_port_slot(str(addressing))
    port = cg.new_Pvariable(port_id, addressing, destination, only_port)
    await cg.register_component(port, {})
    return port


async def _new_encryption(config: ConfigType, lower: MockObj) -> MockObj:
    cg.add_define("USE_PACKET_INTERFACE_XXTEA")
    var = cg.new_Pvariable(config[CONF_ID], lower, config[CONF_ROLLING_CODE_ENABLE])
    # A 256-bit key from the SHA-256 of the key text, as 32-bit words in little endian order
    digest = hashlib.sha256(config[CONF_KEY].encode()).digest()
    key = [int.from_bytes(digest[i : i + 4], "little") for i in range(0, 32, 4)]
    cg.add(var.set_key(key))
    await cg.register_component(var, config)
    return var


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

    Returns the link layer; applications reach the top layer with PacketInterface::top(). With several addressing
    peers, applications use the peers' ids instead.
    """
    var = cg.new_Pvariable(config[CONF_ID], *args)
    cg.add(var.set_name(config[CONF_ID].id))
    cg.add(var.set_max_packet_size(config[CONF_MAX_PACKET_SIZE]))
    await cg.register_component(var, config)
    if (addressing := config.get(CONF_ADDRESSING)) is None:
        await _new_layers(config, var)
        return var
    cg.add_define("USE_PACKET_INTERFACE_BASIC_ADDRESSING")
    hub = cg.new_Pvariable(addressing[CONF_ID], var, addressing[CONF_ADDRESS])
    await cg.register_component(hub, addressing)
    if (peers := addressing.get(CONF_PEERS)) is None:
        port = await _new_port(
            addressing[CONF_PORT_ID], hub, addressing[CONF_DESTINATION], True
        )
        await _new_layers(config, port)
        return var
    # Each peer has its own stack, named after the peer for logs and saved preferences
    for peer in peers:
        port = await _new_port(peer[CONF_ID], hub, peer[CONF_DESTINATION], False)
        cg.add(port.set_name(peer[CONF_ID].id))
        await _new_layers(peer, port)
    return var


async def _new_layers(config: ConfigType, lower: MockObj) -> None:
    """Create the layers configured in config, from the bottom up, above lower."""
    if (encryption := config.get(CONF_ENCRYPTION)) is not None:
        lower = await _new_encryption(encryption, lower)
    if (reliability := config.get(CONF_RELIABILITY)) is not None:
        lower = await _new_reliability(reliability, lower)
    if (session := config.get(CONF_SESSION)) is not None:
        await _new_session(session, lower)
