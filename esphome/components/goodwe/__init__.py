import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv

CODEOWNERS = ["@clydebarrow"]

goodwe_ns = cg.esphome_ns.namespace("goodwe")
Goodwe = goodwe_ns.class_("Goodwe", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Goodwe),
        cv.Required(CONF_TRANSPORT): cv.use_id(transport.Transport),
    }
)
