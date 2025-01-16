import esphome.codegen as cg
from esphome.components.usb_host import (
    USBClient,
    register_usb_client,
    usb_device_schema,
)
import esphome.config_validation as cv
from esphome.const import CONF_ADDRESS, CONF_DEVICE, CONF_ID, CONF_INDEX, CONF_TYPE
from esphome.cpp_helpers import register_component
from esphome.cpp_types import Component

DEPENDENCIES = ["usb_host"]
sicom_ns = cg.esphome_ns.namespace("sicom")
SicomComponent = sicom_ns.class_("SicomComponent", cg.Component, USBClient)

SicomDevice = sicom_ns.class_("SicomDevice", Component)

CONF_SICOM_ID = "sicom_id"
CONF_DEVICES = "devices"


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
    SicomDeviceType("ST107", 3, 4, 0, 1),
    SicomDeviceType("SC301", 0, 1, 0, 0),
    SicomDeviceType("SC303", 0, 0, 1, 0),
]

SI_DEVICES_MAP = {stype.name: stype for stype in SI_DEVICES}

SICOM_SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_DEVICE): cv.use_id(SicomDevice),
        cv.Required(CONF_INDEX): cv.uint8_t,
    }
)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SicomComponent),
        cv.Required(CONF_DEVICES): cv.ensure_list(
            cv.typed_schema(
                {
                    stype.name: cv.polling_component_schema("5s").extend(
                        {
                            cv.Required(CONF_ID): cv.declare_id(stype.cls),
                            cv.Optional(CONF_ADDRESS, default=0): cv.uint8_t,
                        }
                    )
                    for stype in SI_DEVICES
                }
            )
        ),
    }
).extend(usb_device_schema(SicomComponent, 0x1725, 0x07))


async def to_code(config):
    var = await register_usb_client(config)
    for device in config[CONF_DEVICES]:
        stype = SI_DEVICES_MAP[device[CONF_TYPE]]
        device_var = cg.new_Pvariable(device[CONF_ID])
        await register_component(device_var, device)
        cg.add(device_var.set_address(device[CONF_ADDRESS]))
        cg.add(var.add_device(device_var))
