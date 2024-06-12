import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.cpp_types import Component
from esphome.yaml_util import make_data_base
from esphome import pins, automation
from esphome.const import (
    CONF_BAUD_RATE,
    CONF_ID,
    CONF_NUMBER,
    CONF_RX_PIN,
    CONF_TX_PIN,
    CONF_UART_ID,
    CONF_DATA,
    CONF_RX_BUFFER_SIZE,
    CONF_INVERTED,
    CONF_INVERT,
    CONF_TRIGGER_ID,
    CONF_SEQUENCE,
    CONF_TIMEOUT,
    CONF_DEBUG,
    CONF_DIRECTION,
    CONF_AFTER,
    CONF_BYTES,
    CONF_DELIMITER,
    CONF_DUMMY_RECEIVER,
    CONF_DUMMY_RECEIVER_ID,
    CONF_LAMBDA,
)
from esphome.core import CORE

CODEOWNERS = ["@clydebarrow"]
usb_host_ns = cg.esphome_ns.namespace("usb_host")
USBHost = usb_host_ns.class_("USBHost", Component)

CONFIG_SCHEMA = {
    cv.GenerateID(): cv.declare_id(USBHost),
}


async def to_code(config):
    cg.add_global(usb_host_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
