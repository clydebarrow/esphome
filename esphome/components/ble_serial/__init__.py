import esphome.codegen as cg
from esphome.components import esp32_ble, esp32_ble_client, esp32_ble_tracker
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.types import ConfigType

AUTO_LOAD = ["esp32_ble_client"]
CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["esp32_ble_tracker"]
DOMAIN = "ble_serial"

CONF_CLIENTS = "clients"
CONF_MAX_DEVICES = "max_devices"

ble_serial_ns = cg.esphome_ns.namespace("ble_serial")
BleSerial = ble_serial_ns.class_(
    "BleSerial", cg.Component, esp32_ble_tracker.ESPBTDeviceListener
)
BleSerialClient = ble_serial_ns.class_(
    "BleSerialClient", esp32_ble_client.BLEClientBase
)

CLIENT_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(BleSerialClient)}).extend(
    cv.COMPONENT_SCHEMA
)


def _add_clients(config: ConfigType) -> ConfigType:
    if CONF_CLIENTS in config:
        return config
    count: int = config[CONF_MAX_DEVICES]
    esp32_ble.consume_connection_slots(count, DOMAIN)(config)
    return {**config, CONF_CLIENTS: [CLIENT_SCHEMA({}) for _ in range(count)]}


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BleSerial),
            cv.Optional(CONF_MAX_DEVICES, default=2): cv.int_range(
                min=1, max=esp32_ble.IDF_MAX_CONNECTIONS
            ),
        }
    )
    .extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    _add_clients,
)


async def to_code(config: ConfigType) -> None:
    cg.add_define("USE_ESP32_BLE_UUID")
    cg.add_define("BLE_SERIAL_MAX_DEVICES", config[CONF_MAX_DEVICES])
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await esp32_ble_tracker.register_ble_device(var, config)
    for client_config in config[CONF_CLIENTS]:
        client = cg.new_Pvariable(client_config[CONF_ID], var)
        await cg.register_component(client, client_config)
        await esp32_ble_tracker.register_client(client, config)
        cg.add(var.add_client(client))
