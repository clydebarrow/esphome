from typing import Literal

from esphome import pins
import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_ADDRESS, CONF_DISABLE_CRC, CONF_FLOW_CONTROL_PIN, CONF_ID
from esphome.cpp_helpers import gpio_pin_expression
import esphome.final_validate as fv

DEPENDENCIES = ["uart"]

aa55_ns = cg.esphome_ns.namespace("aa55")
AA55 = aa55_ns.class_("AA55", cg.Component, uart.UARTDevice)
AA55Device = aa55_ns.class_("ModbusDevice")
MULTI_CONF = True

CONF_AA55_ID = "aa55_id"
CONF_SEND_WAIT_TIME = "send_wait_time"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AA55),
            cv.Optional(CONF_FLOW_CONTROL_PIN): pins.gpio_output_pin_schema,
            cv.Optional(
                CONF_SEND_WAIT_TIME, default="250ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_DISABLE_CRC, default=False): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    cg.add_global(aa55_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    await uart.register_uart_device(var, config)

    if CONF_FLOW_CONTROL_PIN in config:
        pin = await gpio_pin_expression(config[CONF_FLOW_CONTROL_PIN])
        cg.add(var.set_flow_control_pin(pin))

    cg.add(var.set_send_wait_time(config[CONF_SEND_WAIT_TIME]))
    cg.add(var.set_disable_crc(config[CONF_DISABLE_CRC]))


def modbus_device_schema(default_address):
    schema = {
        cv.GenerateID(CONF_AA55_ID): cv.use_id(AA55),
    }
    if default_address is None:
        schema[cv.Required(CONF_ADDRESS)] = cv.hex_uint8_t
    else:
        schema[cv.Optional(CONF_ADDRESS, default=default_address)] = cv.hex_uint8_t
    return cv.Schema(schema)


def final_validate_modbus_device(
    name: str, *, role: Union[Literal["server", "client"], None] = None
):
    def validate_hub(hub_config):
        hub_schema = {}
        return cv.Schema(hub_schema, extra=cv.ALLOW_EXTRA)(hub_config)

    return cv.Schema(
        {cv.Required(CONF_AA55_ID): fv.id_declaration_match_schema(validate_hub)},
        extra=cv.ALLOW_EXTRA,
    )


async def register_aa55_device(var, config):
    parent = await cg.get_variable(config[CONF_AA55_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(parent.register_device(var))
