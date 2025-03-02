import esphome.codegen as cg
from esphome.components.esp32 import only_on_variant
from esphome.components.esp32.const import VARIANT_ESP32S3
from esphome.components.sun_gtil2.text_sensor import CONF_SERIAL_NUMBER
from esphome.components.uart import UART_DEVICE_SCHEMA, UARTDevice, register_uart_device
import esphome.config_validation as cv
from esphome.config_validation import polling_component_schema
from esphome.const import (
    CONF_DEVICE,
    CONF_ID,
    CONF_INDEX,
    CONF_TX_PIN,
    CONF_TYPE,
    CONF_UPDATE_INTERVAL,
)
from esphome.cpp_helpers import register_component
from esphome.cpp_types import Component
from esphome.pins import gpio_output_pin_schema, internal_gpio_output_pin_number

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["bytebuffer"]
sicom_ns = cg.esphome_ns.namespace("sicom")
SicomComponent = sicom_ns.class_("SicomComponent", cg.Component, UARTDevice)

SicomDevice = sicom_ns.class_("SicomDevice", Component)

CONF_SICOM_ID = "sicom_id"
CONF_DEVICES = "devices"
CONF_TX_ENABLE_PIN = "tx_enable_pin"


class SicomDeviceType:
    def __init__(self, name, voltages=0, resistances=0, currents=0, relays=0):
        self.name = name
        self.voltages = voltages
        self.resistances = resistances
        self.currents = currents
        self.relays = relays
        self.cls = sicom_ns.class_(f"Sicom{name}Device", SicomDevice, Component)


SI_DEVICES = [
    # [name, voltages, resistances, currents, relays]
    SicomDeviceType("SCQ25T", 3, 4, 4, 1),
    SicomDeviceType("ST107", 3, 4, 0, 1),
    SicomDeviceType("SC301", 2, 1, 1, 0),
    SicomDeviceType("SC303", 2, 3, 1, 0),
]

SI_DEVICES_MAP = {stype.name: stype for stype in SI_DEVICES}

SICOM_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_DEVICE): cv.use_id(SicomDevice),
        cv.Required(CONF_INDEX): cv.uint8_t,
    }
)


def validate_config(config):
    for stype in SI_DEVICES:
        devices = [
            device for device in config[CONF_DEVICES] if device[CONF_TYPE] == stype.name
        ]
        if len(devices) > 1:
            serials = {device[CONF_SERIAL_NUMBER] for device in devices}
            if len(serials) != len(devices) or not all(
                device[CONF_SERIAL_NUMBER] for device in devices
            ):
                raise cv.Invalid(
                    "Multiple devices of the same type must each have a unique serial number"
                )
    return config


CONFIG_SCHEMA = cv.All(
    UART_DEVICE_SCHEMA.extend(polling_component_schema("1s")).extend(
        {
            cv.GenerateID(): cv.declare_id(SicomComponent),
            cv.Required(CONF_TX_PIN): internal_gpio_output_pin_number,
            cv.Optional(CONF_TX_ENABLE_PIN): gpio_output_pin_schema,
            cv.Required(CONF_DEVICES): cv.ensure_list(
                cv.typed_schema(
                    {
                        stype.name: cv.polling_component_schema("5s").extend(
                            {
                                cv.Required(CONF_ID): cv.declare_id(stype.cls),
                                cv.Optional(CONF_SERIAL_NUMBER, default=0): cv.uint32_t,
                            }
                        )
                        for stype in SI_DEVICES
                    }
                )
            ),
        }
    ),
    cv.only_with_esp_idf,
    # only_on_variant(supported=[VARIANT_ESP32S3]),
    cv.require_framework_version(esp_idf=cv.Version(5, 1, 0)),
    validate_config,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await register_component(var, config)
    await register_uart_device(var, config)
    devices = config[CONF_DEVICES]
    update_interval = min(
        config[CONF_UPDATE_INTERVAL].total_milliseconds // len(devices), 30
    )
    cg.add(var.set_tx_pin(config[CONF_TX_PIN]))
    if tx_enable_pin := config.get(CONF_TX_ENABLE_PIN):
        tx_enable_pin = await cg.gpio_pin_expression(tx_enable_pin)
        cg.add(var.set_tx_enable_pin(tx_enable_pin))
    cg.add(var.set_update_interval(update_interval))
    for device in config[CONF_DEVICES]:
        device_var = cg.new_Pvariable(device[CONF_ID])
        await register_component(device_var, device)
        cg.add(device_var.set_serial_number(device[CONF_SERIAL_NUMBER]))
        cg.add(var.add_device(device_var))
