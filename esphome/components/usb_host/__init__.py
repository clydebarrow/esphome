import esphome.codegen as cg
from esphome.components.esp32 import add_idf_sdkconfig_option
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.cpp_types import Component

CODEOWNERS = ["@clydebarrow"]
usb_host_ns = cg.esphome_ns.namespace("usb_host")
USBHost = usb_host_ns.class_("USBHost", Component)
USBClient = usb_host_ns.class_("USBClient", Component)

CONF_DEVICES = "devices"
CONF_VID = "vid"
CONF_PID = "pid"


def usb_device_schema(cls=USBClient, vid: int = None, pid: int = None) -> cv.Schema:
    schema = cv.COMPONENT_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(cls),
        }
    )
    if vid:
        schema = schema.extend({cv.Optional(CONF_VID, default=vid): cv.uint16_t})
    else:
        schema = schema.extend({cv.Required(CONF_VID): cv.uint16_t})
    if pid:
        schema = schema.extend({cv.Optional(CONF_PID, default=pid): cv.uint16_t})
    else:
        schema = schema.extend({cv.Required(CONF_PID): cv.uint16_t})
    return schema


CONFIG_SCHEMA = cv.COMPONENT_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(USBHost),
        cv.Optional(CONF_DEVICES): cv.ensure_list(usb_device_schema()),
    }
)


async def register_usb_client(config):
    var = cg.new_Pvariable(config[CONF_VID], config[CONF_PID])
    await cg.register_component(var, config)
    return var


async def to_code(config):
    add_idf_sdkconfig_option("CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE", 1024)
    cg.add_global(usb_host_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    for device in config.get(CONF_DEVICES) or ():
        await register_usb_client(device)
