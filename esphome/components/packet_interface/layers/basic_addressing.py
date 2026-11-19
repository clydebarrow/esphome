"""Addressing layer with 16-bit addresses, carrying one stack of layers or one for each of several peers."""

import copy
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ADDRESS, CONF_ENCRYPTION, CONF_ID, CONF_SIZE, CONF_TYPE
from esphome.core import ID
from esphome.cpp_generator import MockObj
from esphome.types import ConfigType

from . import (
    ADDRESSING,
    BROADCAST_ADDRESS,
    CONF_DESTINATION,
    CONF_PEERS,
    PacketInterface,
    check_session,
    check_single_peer,
    packet_interface_ns,
    remaining_packet_size,
    stack_schema,
)

CONF_PORT_ID = "port_id"
CONF_PEER_POOLS = "peer_pools"
# Set by validation: each slot's port id and its own copy of the layers
CONF_SLOTS = "slots"
CONF_LAYERS = "layers"

# The destination and source addresses
HEADER_SIZE = 4

BasicAddressing = packet_interface_ns.class_("BasicAddressing", cg.Component)
AddressingPort = packet_interface_ns.class_("AddressingPort", PacketInterface)
PeerPool = packet_interface_ns.class_("PeerPool")

_request_port_slot = cg.slot_counter("PACKET_INTERFACE_ADDRESSING_PORT_COUNT")


def _destination(value: Any) -> int:
    """A device address, or broadcast."""
    if isinstance(value, str) and value.lower() == "broadcast":
        return BROADCAST_ADDRESS
    return cv.hex_int_range(1, BROADCAST_ADDRESS)(value)


def _validate_peer(config: ConfigType) -> ConfigType:
    check_session(config)
    check_single_peer(config, config[CONF_DESTINATION])
    return config


def _unique_destinations(peers: list[ConfigType]) -> list[ConfigType]:
    destinations = [peer[CONF_DESTINATION] for peer in peers]
    if len(set(destinations)) != len(destinations):
        raise cv.Invalid("Each peer must have a different destination")
    return peers


def _validate_pool(config: ConfigType) -> ConfigType:
    """A pool's slots change hands at run time, so they can keep no state for one peer, and get their keys then."""
    check_session(config)
    if (encryption := config.get(CONF_ENCRYPTION)) is not None:
        if encryption[CONF_TYPE] != "xxtea" or not encryption["key_at_runtime"]:
            raise cv.Invalid(
                "Each slot's key is set when the slot is assigned, so use 'key_at_runtime: true'",
                path=[CONF_ENCRYPTION],
            )
        if encryption["rolling_code_enable"]:
            raise cv.Invalid(
                "The rolling code keeps state for one peer, so it cannot be used in a pool",
                path=[CONF_ENCRYPTION],
            )
    for key in ("reliability", "session"):
        if key in config:
            raise cv.Invalid(
                f"'{key}' keeps state for one peer, so it cannot be used in a pool",
                path=[key],
            )
    # Each slot is made here rather than in code generation, so that its components are counted with the rest, as
    # the application sizes its list of components from the ids declared in the validated configuration
    pool_id: ID = config[CONF_ID]
    layers = {
        key: value for key, value in config.items() if key not in (CONF_ID, CONF_SIZE)
    }
    config[CONF_SLOTS] = [
        {
            CONF_PORT_ID: ID(
                f"{pool_id.id}_{index}_port", is_declaration=True, type=AddressingPort
            ),
            CONF_LAYERS: _with_new_ids(layers, f"{pool_id.id}_{index}"),
        }
        for index in range(config[CONF_SIZE])
    ]
    return config


def _pools_need_peers(config: ConfigType) -> ConfigType:
    """A link with pools has a stack for each peer, so the rest of its stacks go in peers as well."""
    if CONF_PEER_POOLS in config and CONF_PEERS not in config:
        raise cv.Invalid(
            f"With '{CONF_PEER_POOLS}', give the other stacks as '{CONF_PEERS}', such as one for broadcast",
            path=[CONF_PEER_POOLS],
        )
    return config


def _not_own_address(config: ConfigType) -> ConfigType:
    """Packets from this device's own address are dropped as echoes, so it cannot be a destination."""
    address = config[CONF_ADDRESS]
    if config.get(CONF_DESTINATION) == address:
        raise cv.Invalid(
            "Must not be this device's own address", path=[CONF_DESTINATION]
        )
    for index, peer in enumerate(config.get(CONF_PEERS, [])):
        if peer[CONF_DESTINATION] == address:
            raise cv.Invalid(
                "Must not be this device's own address",
                path=[CONF_PEERS, index, CONF_DESTINATION],
            )
    return config


# A function, as each peer holds the other layers, whose schemas exist only once every layer type is registered
def _basic_addressing_schema() -> cv.Schema:
    peer_schema = cv.All(
        cv.Schema(
            {
                cv.Required(CONF_ID): cv.declare_id(AddressingPort),
                cv.Required(CONF_DESTINATION): _destination,
                **stack_schema(),
            }
        ),
        _validate_peer,
    )
    pool_schema = cv.All(
        cv.Schema(
            {
                cv.Required(CONF_ID): cv.declare_id(PeerPool),
                cv.Required(CONF_SIZE): cv.int_range(1, 64),
                **stack_schema(),
            }
        ),
        _validate_pool,
    )
    return cv.All(
        cv.Schema(
            {
                cv.GenerateID(): cv.declare_id(BasicAddressing),
                cv.GenerateID(CONF_PORT_ID): cv.declare_id(AddressingPort),
                cv.Required(CONF_ADDRESS): cv.hex_int_range(1, BROADCAST_ADDRESS - 1),
                cv.Optional(CONF_DESTINATION): _destination,
                cv.Optional(CONF_PEERS): cv.All(
                    cv.ensure_list(peer_schema), cv.Length(min=1), _unique_destinations
                ),
                # Stacks whose destinations and keys the application sets at run time
                cv.Optional(CONF_PEER_POOLS): cv.All(
                    cv.ensure_list(pool_schema), cv.Length(min=1)
                ),
            }
        ).extend(cv.COMPONENT_SCHEMA),
        cv.has_at_most_one_key(CONF_DESTINATION, CONF_PEERS),
        _pools_need_peers,
        _not_own_address,
    )


async def _new_port(
    port_id: ID, addressing: MockObj, destination: int, only_port: bool
) -> MockObj:
    _request_port_slot(str(addressing))
    port = cg.new_Pvariable(port_id, addressing, destination, only_port)
    await cg.register_component(port, {})
    return port


@ADDRESSING.register(
    "basic",
    schema_factory=_basic_addressing_schema,
    packet_size=lambda config, size: remaining_packet_size(size, HEADER_SIZE),
)
async def build_basic_addressing(
    config: ConfigType, link: MockObj
) -> list[tuple[MockObj, ConfigType | None]]:
    cg.add_define("USE_PACKET_INTERFACE_BASIC_ADDRESSING")
    hub = cg.new_Pvariable(config[CONF_ID], link, config[CONF_ADDRESS])
    await cg.register_component(hub, config)
    if (peers := config.get(CONF_PEERS)) is None:
        port = await _new_port(
            config[CONF_PORT_ID], hub, config[CONF_DESTINATION], True
        )
        return [(port, None)]
    stacks = []
    # Each peer has its own stack, named after the peer for logs and saved preferences
    for peer in peers:
        port = await _new_port(peer[CONF_ID], hub, peer[CONF_DESTINATION], False)
        cg.add(port.set_name(peer[CONF_ID].id))
        stacks.append((port, peer))
    for pool in config.get(CONF_PEER_POOLS, []):
        await _build_pool(pool, hub)
    return stacks


def _with_new_ids(config: ConfigType, prefix: str) -> ConfigType:
    """A copy of a stack's layers with ids of their own, as each slot of a pool has the same layers.

    Generated ids are still unresolved during validation, so each copy gets its own when ids are resolved."""

    def renamed(value: Any) -> Any:
        if isinstance(value, ID):
            if not value.is_declaration:
                return value
            name = None if value.id is None else f"{prefix}_{value.id}"
            return ID(name, is_declaration=True, type=value.type)
        if isinstance(value, dict):
            return value.__class__((key, renamed(item)) for key, item in value.items())
        if isinstance(value, list):
            return [renamed(item) for item in value]
        return value

    return renamed(copy.deepcopy(config))


async def _build_pool(config: ConfigType, hub: MockObj) -> None:
    # Imported here, as the packet_interface component imports this module before it defines build_stack
    from .. import build_stack

    pool_id: ID = config[CONF_ID]
    pool = cg.new_Pvariable(pool_id, pool_id.id)
    for index, slot in enumerate(config[CONF_SLOTS]):
        layers = slot[CONF_LAYERS]
        port = await _new_port(slot[CONF_PORT_ID], hub, 0, False)
        cg.add(port.set_name(f"{pool_id.id}_{index}"))
        await build_stack(layers, port)
        encryption = cg.nullptr
        if (layer := layers.get(CONF_ENCRYPTION)) is not None:
            encryption = await cg.get_variable(layer[CONF_ID])
        cg.add(pool.add_slot(port, encryption))
