"""Unit tests for the packet_interface component and its platforms."""

import pytest

from esphome import config_validation as cv
from esphome.components import packet_interface
from esphome.components.packet_interface import (
    CONF_MAX_PACKET_SIZE,
    CONF_MAX_QUEUE_LENGTH,
    CONF_RELIABILITY,
)
from esphome.components.uart import packet_interface as uart_packet_interface
from esphome.components.udp import packet_interface as udp_packet_interface


@pytest.mark.parametrize(
    ("schema", "default", "limit"),
    [
        (uart_packet_interface.CONFIG_SCHEMA, 508, 4096),
        (udp_packet_interface.CONFIG_SCHEMA, 508, 508),
    ],
)
def test_platform_max_packet_size(
    setup_core, schema: cv.Schema, default: int, limit: int
) -> None:
    """Each platform has its own default and upper limit for max_packet_size."""
    assert schema({})[CONF_MAX_PACKET_SIZE] == default
    assert schema({CONF_MAX_PACKET_SIZE: limit})[CONF_MAX_PACKET_SIZE] == limit
    with pytest.raises(cv.Invalid):
        schema({CONF_MAX_PACKET_SIZE: limit + 1})
    with pytest.raises(cv.Invalid):
        schema({CONF_MAX_PACKET_SIZE: 15})


def test_schema_default(setup_core) -> None:
    """packet_interface_schema applies the default it is given."""
    schema = packet_interface.packet_interface_schema(
        packet_interface.PacketInterface, 100
    )
    assert schema({})[CONF_MAX_PACKET_SIZE] == 100


def test_reliability_defaults(setup_core) -> None:
    """An empty reliability key selects the HDLC layer with its defaults."""
    config = uart_packet_interface.CONFIG_SCHEMA({CONF_RELIABILITY: None})
    reliability = config[CONF_RELIABILITY]
    assert reliability["type"] == "hdlc"
    assert reliability["timeout"].total_milliseconds == 200
    assert reliability[CONF_MAX_QUEUE_LENGTH] == 4
    assert reliability["max_retries"] == 5


def test_reliability_queue_length_limit(setup_core) -> None:
    """The queue cannot hold more frames than 3 bit sequence numbers allow."""
    with pytest.raises(cv.Invalid):
        uart_packet_interface.CONFIG_SCHEMA(
            {CONF_RELIABILITY: {CONF_MAX_QUEUE_LENGTH: 8}}
        )
