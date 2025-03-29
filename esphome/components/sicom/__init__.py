import esphome.codegen as cg
from esphome.components.sun_gtil2.text_sensor import CONF_SERIAL_NUMBER
from esphome.components.uart import (
    UART_DEVICE_SCHEMA,
    IDFUARTComponent,
    UARTDevice,
    register_uart_device,
)
import esphome.config_validation as cv
from esphome.config_validation import polling_component_schema
from esphome.const import (
    CONF_DEVICE,
    CONF_ID,
    CONF_INDEX,
    CONF_TX_PIN,
    CONF_TYPE,
    CONF_UART_ID,
    CONF_UPDATE_INTERVAL,
)
from esphome.cpp_helpers import register_component
from esphome.cpp_types import Component
from esphome.pins import gpio_output_pin_schema, internal_gpio_output_pin_number

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["bytebuffer"]
sicom_ns = cg.esphome_ns.namespace("sicom")
SicomComponent = sicom_ns.class_("SicomComponent", cg.Component, UARTDevice)

SicomDevice = sicom_ns.class_("SicomDevice")
SicomSensor = sicom_ns.class_("SicomSensor")

CONF_SICOM_ID = "sicom_id"
CONF_DEVICES = "devices"
CONF_TX_ENABLE_PIN = "tx_enable_pin"


class SicomDeviceType:
    def __init__(self, name,
                 id,
                 voltage_count = 0,
                 voltage_offset = 0,
                 voltage_size = 2,
                 voltage_scale = .01,
                 voltage_increment = 0,
                 current_count = 0,
                    current_offset = 0,
                    current_size = 4,
                    current_scale = .001,
                 current_increment = 0,
                    resistance_count = 0,
                    resistance_offset = 0,
                    resistance_size = 2,
                    resistance_scale = 1.0,
                    resistance_increment = 0,
                 ):

        self.name = name
        self.id = id
        self.voltage_count = voltage_count
        self.voltage_offset = voltage_offset
        self.voltage_size = voltage_size
        self.voltage_scale = voltage_scale
        self.voltage_increment = voltage_increment or voltage_size
        self.current_count = current_count
        self.current_offset = current_offset
        self.current_size = current_size
        self.current_scale = current_scale
        self.current_increment = current_increment or current_size
        self.resistance_count = resistance_count
        self.resistance_offset = resistance_offset
        self.resistance_size = resistance_size
        self.resistance_scale = resistance_scale
        self.resistance_increment = resistance_increment or resistance_size




SI_DEVICES = [
    SicomDeviceType("SCQ25T", id=0x2, voltage_count=3, voltage_offset=28,
                    current_count=4, current_offset=4, current_size=2,
                    resistance_count = 4, resistance_offset = 34),
    SicomDeviceType("ST107", id=3,
                    voltage_count=4, voltage_offset=4,
                    resistance_count=4, resistance_offset=10,),
    SicomDeviceType("SC301", id= 0xE, voltage_count=2, voltage_offset=13, voltage_increment= 3,
                    current_count=1, current_offset=4),
    SicomDeviceType("SC303", id= 0x10, voltage_count=2, voltage_offset=14,
                    resistance_count=3, resistance_offset=18,
                    current_count=1, current_offset=4),
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
    polling_component_schema("1s").extend(
        {
            cv.GenerateID(CONF_UART_ID): cv.use_id(IDFUARTComponent),
            cv.GenerateID(): cv.declare_id(SicomComponent),
            cv.Required(CONF_TX_PIN): internal_gpio_output_pin_number,
            cv.Optional(CONF_TX_ENABLE_PIN): gpio_output_pin_schema,
            cv.Required(CONF_DEVICES): cv.ensure_list(
                cv.typed_schema(
                    {
                        stype.name: cv.polling_component_schema("5s").extend(
                            {
                                cv.Required(CONF_ID): cv.declare_id(SicomDevice),
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
