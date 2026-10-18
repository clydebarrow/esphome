from collections.abc import Callable
import importlib
import pkgutil
from typing import Any

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import ID
from esphome.cpp_generator import MockObj, MockObjClass
import esphome.final_validate as fv
from esphome.types import ConfigType

from . import layers
from .layers import (
    ADDRESSING,
    BROADCAST_ADDRESS,
    CONF_ADDRESSING,
    CONF_DESTINATION,
    CONF_PEERS,
    STACK_ORDER,
    PacketInterface,
    build_stack,
    check_single_peer,
    packet_interface_ns,
    stack_schema,
)

AUTO_LOAD = ["binary_sensor", "xxtea"]
CODEOWNERS = ["@clydebarrow"]
DOMAIN = "packet_interface"
IS_PLATFORM_COMPONENT = True

CONF_MAX_PACKET_SIZE = "max_packet_size"
CONF_PACKET_INTERFACE = "packet_interface"

# Each module in layers registers a layer type; import them all before building the schemas from the registry
for module_info in pkgutil.iter_modules(layers.__path__):
    importlib.import_module(f".layers.{module_info.name}", package=__package__)

LINK_LAYER_SCHEMAS = {
    cv.Optional(CONF_ADDRESSING): ADDRESSING.schema(),
    **stack_schema(),
}

__all__ = [
    "CONF_MAX_PACKET_SIZE",
    "CONF_PACKET_INTERFACE",
    "PacketInterface",
    "final_validate_interface",
    "new_packet_interface",
    "packet_interface_ns",
    "packet_interface_schema",
]


def _validate_layers(config: ConfigType) -> ConfigType:
    """With peers, the layers go in each peer; otherwise check them against the one destination."""
    if (addressing := config.get(CONF_ADDRESSING)) is None:
        return config
    if CONF_PEERS in addressing:
        for kind in STACK_ORDER:
            if kind.key in config:
                raise cv.Invalid(
                    f"With '{CONF_PEERS}', '{kind.key}' goes in each peer",
                    path=[kind.key],
                )
        return config
    addressing.setdefault(CONF_DESTINATION, BROADCAST_ADDRESS)
    check_single_peer(config, addressing[CONF_DESTINATION])
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
    provides: tuple[str, ...] = (),
) -> cv.Schema:
    """Schema for a link platform; provides names the layer keys, such as reliability, that the link does itself."""
    link_layers = {
        key: schema for key, schema in LINK_LAYER_SCHEMAS.items() if key not in provides
    }
    return (
        cv.Schema(
            {
                cv.GenerateID(): cv.declare_id(class_),
                cv.Optional(
                    CONF_MAX_PACKET_SIZE, default=default_max_packet_size
                ): cv.int_range(16, max_packet_size_limit),
                **link_layers,
            }
        )
        .extend(cv.COMPONENT_SCHEMA)
        .add_extra(_validate_layers)
    )


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
        await build_stack(config, var)
        return var
    for port, layers_config in await ADDRESSING.get_type(addressing).build(
        addressing, var
    ):
        await build_stack(config if layers_config is None else layers_config, port)
    return var
