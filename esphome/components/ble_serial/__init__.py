"""A one-way serial stream over Bluetooth LE, on the HM-10 and Nordic UART services, as flight apps read a FLARM."""

import esphome.codegen as cg
from esphome.components import esp32_ble
from esphome.components.esp32_ble_server import (
    BLECharacteristic,
    BLECharacteristic_ns,
    BLEDescriptor,
    BLEServer,
    BLEService,
    esp32_ble_server_ns,
    parse_uuid,
)
import esphome.config_validation as cv
from esphome.const import CONF_BUFFER_SIZE, CONF_ID
from esphome.types import ConfigType

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["esp32_ble_server"]
DOMAIN = "ble_serial"

CONF_BLE_SERVER_ID = "ble_server_id"
CONF_HM10 = "hm10"
CONF_NORDIC_UART = "nordic_uart"
# The GATT objects, declared so that code can be generated for them
CONF_HM10_SERVICE_ID_ = "hm10_service_id_"
CONF_HM10_CHARACTERISTIC_ID_ = "hm10_characteristic_id_"
CONF_HM10_CCCD_ID_ = "hm10_cccd_id_"
CONF_NUS_SERVICE_ID_ = "nus_service_id_"
CONF_NUS_RX_ID_ = "nus_rx_id_"
CONF_NUS_TX_ID_ = "nus_tx_id_"
CONF_NUS_CCCD_ID_ = "nus_cccd_id_"

# The HM-10 module's service, which XCSoar, LK8000 and other flight apps read a BLE serial device through
HM10_SERVICE_UUID = 0xFFE0
HM10_CHARACTERISTIC_UUID = 0xFFE1
NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

ble_serial_ns = cg.esphome_ns.namespace("ble_serial")
BLESerial = ble_serial_ns.class_("BLESerial", cg.Component)
# The client configuration descriptor, through which a client turns notifications on
BLE2902 = esp32_ble_server_ns.class_("BLE2902", BLEDescriptor)


def _validate(config: ConfigType) -> ConfigType:
    if not config[CONF_HM10] and not config[CONF_NORDIC_UART]:
        raise cv.Invalid("At least one of hm10 and nordic_uart must be enabled")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BLESerial),
            cv.GenerateID(esp32_ble.CONF_BLE_ID): cv.use_id(esp32_ble.ESP32BLE),
            cv.GenerateID(CONF_BLE_SERVER_ID): cv.use_id(BLEServer),
            cv.Optional(CONF_HM10, default=True): cv.boolean,
            cv.Optional(CONF_NORDIC_UART, default=True): cv.boolean,
            cv.Optional(CONF_BUFFER_SIZE, default=2048): cv.int_range(
                min=256, max=16384
            ),
            cv.GenerateID(CONF_HM10_SERVICE_ID_): cv.declare_id(BLEService),
            cv.GenerateID(CONF_HM10_CHARACTERISTIC_ID_): cv.declare_id(
                BLECharacteristic
            ),
            cv.GenerateID(CONF_HM10_CCCD_ID_): cv.declare_id(BLE2902),
            cv.GenerateID(CONF_NUS_SERVICE_ID_): cv.declare_id(BLEService),
            cv.GenerateID(CONF_NUS_RX_ID_): cv.declare_id(BLECharacteristic),
            cv.GenerateID(CONF_NUS_TX_ID_): cv.declare_id(BLECharacteristic),
            cv.GenerateID(CONF_NUS_CCCD_ID_): cv.declare_id(BLE2902),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate,
)


def _characteristic(service, char_id, uuid, properties, cccd_id=None):
    characteristic = cg.Pvariable(
        char_id,
        service.create_characteristic(
            parse_uuid(uuid), properties, cg.RawExpression("ESP_GATT_PERM_WRITE")
        ),
    )
    if cccd_id is not None:
        cg.add(characteristic.add_descriptor(cg.new_Pvariable(cccd_id)))
    return characteristic


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_BUFFER_SIZE])
    await cg.register_component(var, config)
    server = await cg.get_variable(config[CONF_BLE_SERVER_ID])
    ble = await cg.get_variable(config[esp32_ble.CONF_BLE_ID])
    esp32_ble.register_gatts_event_handler(ble, var)

    if config[CONF_HM10]:
        # One characteristic both ways, as the module has: a handle for the service, two for the characteristic and
        # one for its client configuration descriptor. Its short UUID fits in the advertisement, so apps find it.
        service = cg.Pvariable(
            config[CONF_HM10_SERVICE_ID_],
            server.create_service(parse_uuid(HM10_SERVICE_UUID), True, 4),
        )
        characteristic = _characteristic(
            service,
            config[CONF_HM10_CHARACTERISTIC_ID_],
            HM10_CHARACTERISTIC_UUID,
            BLECharacteristic_ns.PROPERTY_READ
            | BLECharacteristic_ns.PROPERTY_WRITE_NR
            | BLECharacteristic_ns.PROPERTY_NOTIFY,
            config[CONF_HM10_CCCD_ID_],
        )
        cg.add(var.add_characteristic(characteristic))
        cg.add(server.enqueue_start_service(service))
    if config[CONF_NORDIC_UART]:
        # Found by its UUID once connected; a 128-bit UUID would not fit beside the name in the advertisement. The
        # service, RX, and TX with its descriptor.
        service = cg.Pvariable(
            config[CONF_NUS_SERVICE_ID_],
            server.create_service(parse_uuid(NUS_SERVICE_UUID), False, 6),
        )
        _characteristic(
            service,
            config[CONF_NUS_RX_ID_],
            NUS_RX_UUID,
            BLECharacteristic_ns.PROPERTY_WRITE
            | BLECharacteristic_ns.PROPERTY_WRITE_NR,
        )
        tx = _characteristic(
            service,
            config[CONF_NUS_TX_ID_],
            NUS_TX_UUID,
            BLECharacteristic_ns.PROPERTY_NOTIFY,
            config[CONF_NUS_CCCD_ID_],
        )
        cg.add(var.add_characteristic(tx))
        cg.add(server.enqueue_start_service(service))
    cg.add(server.set_advertising_required(True))
