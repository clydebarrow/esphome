import esphome.codegen as cg
from esphome.components import esp32_ble, packet_interface
from esphome.components.esp32_ble import bt_uuid
from esphome.components.packet_interface import (
    CONF_MAX_PACKET_SIZE,
    packet_interface_schema,
)
import esphome.config_validation as cv
from esphome.const import CONF_CHARACTERISTIC_UUID, CONF_SERVICE_UUID
from esphome.types import ConfigType

from .. import (
    BLECharacteristic,
    BLECharacteristic_ns,
    BLEDescriptor,
    BLEServer,
    BLEService,
    esp32_ble_server_ns,
    parse_uuid,
)

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["esp32_ble_server"]

# The service and characteristic a BLE packet interface link uses unless others are configured; the client platform
# in ble_client uses the same defaults, so two ESPHome devices find each other with no UUIDs configured
DEFAULT_SERVICE_UUID = "77370934-02F5-45D0-8D08-E5CB182ABCD4"
DEFAULT_CHARACTERISTIC_UUID = "7C4D9B27-F9FF-42D5-9C66-7F2C1AB988D6"

# A notification carries at most the ATT MTU less 3 bytes, and ESP-IDF negotiates an MTU of up to 517
MAX_PACKET_SIZE = 514
# Fits in one notification at an MTU of 247, which phones and most other devices accept
DEFAULT_PACKET_SIZE = 244

CONF_BLE_SERVER_ID = "ble_server_id"
CONF_SERVICE_ID_ = "service_id_"
CONF_CHARACTERISTIC_ID_ = "characteristic_id_"
CONF_CCCD_ID_ = "cccd_id_"

# The client configuration descriptor, through which a client turns notifications on
BLE2902 = esp32_ble_server_ns.class_("BLE2902", BLEDescriptor)
BLEServerPacketInterface = esp32_ble_server_ns.class_(
    "BLEServerPacketInterface", packet_interface.PacketInterface
)

CONFIG_SCHEMA = packet_interface_schema(
    BLEServerPacketInterface, DEFAULT_PACKET_SIZE, MAX_PACKET_SIZE
).extend(
    {
        cv.GenerateID(esp32_ble.CONF_BLE_ID): cv.use_id(esp32_ble.ESP32BLE),
        cv.GenerateID(CONF_BLE_SERVER_ID): cv.use_id(BLEServer),
        cv.Optional(CONF_SERVICE_UUID, default=DEFAULT_SERVICE_UUID): bt_uuid,
        cv.Optional(
            CONF_CHARACTERISTIC_UUID, default=DEFAULT_CHARACTERISTIC_UUID
        ): bt_uuid,
        cv.GenerateID(CONF_SERVICE_ID_): cv.declare_id(BLEService),
        cv.GenerateID(CONF_CHARACTERISTIC_ID_): cv.declare_id(BLECharacteristic),
        cv.GenerateID(CONF_CCCD_ID_): cv.declare_id(BLE2902),
    }
)


async def to_code(config: ConfigType) -> None:
    var = await packet_interface.new_packet_interface(
        config, config[CONF_MAX_PACKET_SIZE]
    )
    server = await cg.get_variable(config[CONF_BLE_SERVER_ID])
    ble = await cg.get_variable(config[esp32_ble.CONF_BLE_ID])
    esp32_ble.register_gatts_event_handler(ble, var)

    # One characteristic carries packets both ways: the client writes, the server notifies. A service needs a handle
    # for itself, two for the characteristic and one for its client configuration descriptor.
    service = cg.Pvariable(
        config[CONF_SERVICE_ID_],
        server.create_service(parse_uuid(config[CONF_SERVICE_UUID]), True, 4),
    )
    characteristic = cg.Pvariable(
        config[CONF_CHARACTERISTIC_ID_],
        service.create_characteristic(
            parse_uuid(config[CONF_CHARACTERISTIC_UUID]),
            BLECharacteristic_ns.PROPERTY_WRITE
            | BLECharacteristic_ns.PROPERTY_WRITE_NR
            | BLECharacteristic_ns.PROPERTY_NOTIFY,
            cg.RawExpression("ESP_GATT_PERM_WRITE"),
        ),
    )
    cccd = cg.new_Pvariable(config[CONF_CCCD_ID_])
    cg.add(characteristic.add_descriptor(cccd))
    cg.add(server.enqueue_start_service(service))
    # Clients find the link by its service, so the server advertises even with no services of its own configured
    cg.add(server.set_advertising_required(True))
    cg.add(var.set_characteristic(characteristic))
