from esphome import automation, pins
import esphome.codegen as cg
from esphome.components.esp32 import add_idf_sdkconfig_option
import esphome.config_validation as cv
from esphome.const import (
    CONF_AFTER,
    CONF_BAUD_RATE,
    CONF_BYTES,
    CONF_DATA,
    CONF_DEBUG,
    CONF_DELIMITER,
    CONF_DIRECTION,
    CONF_DUMMY_RECEIVER,
    CONF_DUMMY_RECEIVER_ID,
    CONF_ID,
    CONF_INVERT,
    CONF_INVERTED,
    CONF_LAMBDA,
    CONF_NUMBER,
    CONF_RX_BUFFER_SIZE,
    CONF_RX_PIN,
    CONF_SEQUENCE,
    CONF_TIMEOUT,
    CONF_TRIGGER_ID,
    CONF_TX_PIN,
    CONF_UART_ID,
)
from esphome.core import CORE
from esphome.cpp_types import Component, PollingComponent
import esphome.final_validate as fv
from esphome.yaml_util import make_data_base

CODEOWNERS = ["@clydebarrow"]
usb_host_ns = cg.esphome_ns.namespace("usb_host")
USBHost = usb_host_ns.class_("USBHost", Component)
USBClient = usb_host_ns.class_("USBClient", Component)

CONF_DEVICES = "devices"
CONF_VID = "vid"
CONF_PID = "pid"

CONFIG_SCHEMA = cv.COMPONENT_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(USBHost),
        cv.Required(CONF_DEVICES): cv.ensure_list(
            cv.COMPONENT_SCHEMA.extend(
                {
                    cv.GenerateID(): cv.declare_id(USBClient),
                    cv.Required(CONF_VID): cv.uint16_t,
                    cv.Required(CONF_PID): cv.uint16_t,
                }
            )
        ),
    }
)


async def to_code(config):
    add_idf_sdkconfig_option("CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE", 1024)
    cg.add_global(usb_host_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    for device in config[CONF_DEVICES]:
        var = cg.new_Pvariable(device[CONF_ID], device[CONF_VID], device[CONF_PID])
        await cg.register_component(var, device)
