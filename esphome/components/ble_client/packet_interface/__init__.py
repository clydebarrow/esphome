import esphome.codegen as cg
from esphome.components import ble_client, packet_interface
from esphome.components.esp32_ble import bt_uuid
from esphome.components.esp32_ble_server import parse_uuid
from esphome.components.esp32_ble_server.packet_interface import (
    DEFAULT_CHARACTERISTIC_UUID,
    DEFAULT_PACKET_SIZE,
    DEFAULT_SERVICE_UUID,
    MAX_PACKET_SIZE,
)
from esphome.components.packet_interface import (
    CONF_MAX_PACKET_SIZE,
    packet_interface_schema,
)
import esphome.config_validation as cv
from esphome.const import CONF_CHARACTERISTIC_UUID, CONF_ID, CONF_SERVICE_UUID
from esphome.types import ConfigType

from .. import ble_client_ns

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["ble_client"]

BLEClientPacketInterface = ble_client_ns.class_(
    "BLEClientPacketInterface",
    packet_interface.PacketInterface,
    ble_client.BLEClientNode,
)

CONFIG_SCHEMA = (
    packet_interface_schema(
        BLEClientPacketInterface, DEFAULT_PACKET_SIZE, MAX_PACKET_SIZE
    )
    .extend(
        {
            cv.Optional(CONF_SERVICE_UUID, default=DEFAULT_SERVICE_UUID): bt_uuid,
            cv.Optional(
                CONF_CHARACTERISTIC_UUID, default=DEFAULT_CHARACTERISTIC_UUID
            ): bt_uuid,
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
)


async def to_code(config: ConfigType) -> None:
    tx_buffer = cg.static_buffer(
        config[CONF_ID], cg.uint8, config[CONF_MAX_PACKET_SIZE]
    )
    var = await packet_interface.new_packet_interface(config, tx_buffer)
    await ble_client.register_ble_node(var, config)
    cg.add(
        var.set_uuids(
            parse_uuid(config[CONF_SERVICE_UUID]),
            parse_uuid(config[CONF_CHARACTERISTIC_UUID]),
        )
    )
