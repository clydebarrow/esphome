import esphome.codegen as cg
from esphome.components.ags10.sensor import CONF_RESISTANCE
from esphome.components.binary_sensor import binary_sensor_schema, new_binary_sensor
from esphome.components.sun_gtil2.text_sensor import CONF_SERIAL_NUMBER
from esphome.components.uart import IDFUARTComponent, UARTDevice, register_uart_device
import esphome.config_validation as cv
from esphome.config_validation import polling_component_schema
from esphome.const import (
    CONF_CURRENT,
    CONF_DEVICE,
    CONF_ID,
    CONF_INDEX,
    CONF_STATUS,
    CONF_TX_PIN,
    CONF_TYPE,
    CONF_UART_ID,
    CONF_VOLTAGE,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from esphome.cpp_helpers import register_component
from esphome.pins import gpio_output_pin_schema, internal_gpio_output_pin_number

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["bytebuffer", "sensor", "binary_sensor"]
sicom_ns = cg.esphome_ns.namespace("sicom")
SicomComponent = sicom_ns.class_("SicomComponent", cg.Component, UARTDevice)

SicomDevice = sicom_ns.class_("SicomDevice")
SicomSensor = sicom_ns.class_("SicomSensor")
DataType = sicom_ns.enum("DataType")

CONF_SICOM_ID = "sicom_id"
CONF_DEVICES = "devices"
CONF_TX_ENABLE_PIN = "tx_enable_pin"
CONF_SICOM_SENSOR_ID = "sicom_sensor_id"


def get_type(size, signed):
    if size == 2:
        return DataType.SIGNED16 if signed else DataType.UNSIGNED16
    elif size == 4:
        return DataType.SIGNED32 if signed else DataType.UNSIGNED32
    raise ValueError(f"Invalid size {size} for signed {signed}")


class SicomValue:
    def __init__(self, offset, data_type, scale, increment):
        self.offset = offset
        self.data_type = data_type
        self.scale = scale
        self.increment = increment


class SicomDeviceType:
    def __init__(
        self,
        name,
        id,
        voltage_count=0,
        voltage_offset=0,
        voltage_size=2,
        voltage_scale=0.001,
        voltage_increment=0,
        current_count=0,
        current_offset=0,
        current_size=4,
        current_scale=0.001,
        current_increment=0,
        resistance_count=0,
        resistance_offset=0,
        resistance_size=2,
        resistance_scale=1.0,
        resistance_increment=0,
    ):
        self.name = name
        self.id = id
        self.sensors = {}
        voltage_increment = voltage_increment or voltage_size
        voltage_type = get_type(voltage_size, True)
        self.sensors[CONF_VOLTAGE] = [
            SicomValue(
                voltage_offset + offset * voltage_increment,
                voltage_type,
                voltage_scale,
                voltage_increment,
            )
            for offset in range(voltage_count)
        ]
        current_increment = current_increment or current_size
        current_type = get_type(current_size, True)
        self.sensors[CONF_CURRENT] = [
            SicomValue(
                current_offset + offset * current_increment,
                current_type,
                current_scale,
                current_increment,
            )
            for offset in range(current_count)
        ]
        resistance_increment = resistance_increment or resistance_size
        resistance_type = get_type(resistance_size, False)
        self.sensors[CONF_RESISTANCE] = [
            SicomValue(
                resistance_offset + offset * resistance_increment,
                resistance_type,
                resistance_scale,
                resistance_increment,
            )
            for offset in range(resistance_count)
        ]


SI_DEVICES = [
    SicomDeviceType(
        "SCQ25T",
        id=0x2,
        voltage_count=3,
        voltage_offset=28,
        current_count=4,
        current_offset=4,
        current_size=2,
        resistance_count=4,
        resistance_offset=34,
    ),
    SicomDeviceType(
        "ST107",
        id=3,
        voltage_count=4,
        voltage_offset=4,
        resistance_count=4,
        resistance_offset=10,
    ),
    SicomDeviceType(
        "SC301",
        id=0xE,
        voltage_count=2,
        voltage_offset=13,
        voltage_increment=3,
        current_count=1,
        current_offset=4,
        current_scale=0.01,
    ),
    SicomDeviceType(
        "SC303",
        id=0x10,
        voltage_count=2,
        voltage_offset=14,
        resistance_count=3,
        resistance_offset=18,
        current_count=1,
        current_offset=4,
        current_scale=0.01,
    ),
]

SI_DEVICES_MAP = {stype.name: stype for stype in SI_DEVICES}

SICOM_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_DEVICE): cv.use_id(SicomDevice),
        cv.Required(CONF_INDEX): cv.uint8_t,
        cv.GenerateID(CONF_SICOM_SENSOR_ID): cv.declare_id(SicomSensor),
    }
)

sicom_devices = {}


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
    polling_component_schema("15ms").extend(
        {
            cv.GenerateID(CONF_UART_ID): cv.use_id(IDFUARTComponent),
            cv.GenerateID(): cv.declare_id(SicomComponent),
            cv.Required(CONF_TX_PIN): internal_gpio_output_pin_number,
            cv.Optional(CONF_TX_ENABLE_PIN): gpio_output_pin_schema,
            cv.Required(CONF_DEVICES): cv.All(
                cv.ensure_list(
                    cv.typed_schema(
                        {
                            stype.name: cv.Schema(
                                {
                                    cv.Required(CONF_ID): cv.declare_id(SicomDevice),
                                    cv.Optional(CONF_STATUS): binary_sensor_schema(
                                        device_class=DEVICE_CLASS_CONNECTIVITY,
                                        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                                    ),
                                    cv.Optional(
                                        CONF_SERIAL_NUMBER, default=0
                                    ): cv.uint32_t,
                                }
                            )
                            for stype in SI_DEVICES
                        }
                    )
                ),
                cv.Length(min=1, max=31),
            ),
        }
    ),
    cv.only_with_esp_idf,
    cv.require_framework_version(esp_idf=cv.Version(5, 1, 0)),
    validate_config,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await register_component(var, config)
    await register_uart_device(var, config)
    cg.add(var.set_tx_pin(config[CONF_TX_PIN]))
    if tx_enable_pin := config.get(CONF_TX_ENABLE_PIN):
        tx_enable_pin = await cg.gpio_pin_expression(tx_enable_pin)
        cg.add(var.set_tx_enable_pin(tx_enable_pin))
    for device in config[CONF_DEVICES]:
        dtype = SI_DEVICES_MAP[device[CONF_TYPE]]
        sicom_devices[device[CONF_ID]] = dtype
        device_var = cg.new_Pvariable(device[CONF_ID], dtype.id)
        if serial_number := device.get(CONF_SERIAL_NUMBER):
            cg.add(device_var.set_serial_number(serial_number))
        cg.add(var.add_device(device_var))
        if status := device.get(CONF_STATUS):
            sens = await new_binary_sensor(status)
            cg.add(device_var.set_status_sensor(sens))
