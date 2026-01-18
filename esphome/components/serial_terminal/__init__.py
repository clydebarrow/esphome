"""Serial Terminal component for ESPHome web server."""
from __future__ import annotations

import esphome.codegen as cg
from esphome.components import uart, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_UART_ID, PLATFORM_ESP32, PLATFORM_ESP8266
from esphome.core import CORE

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["web_server_base", "uart"]

serial_terminal_ns = cg.esphome_ns.namespace("serial_terminal")
SerialTerminal = serial_terminal_ns.class_("SerialTerminal", cg.Component)

CONF_SERIAL_TERMINALS = "serial_terminals"
CONF_PATH = "path"
CONF_BUFFER_SIZE = "buffer_size"

SERIAL_TERMINAL_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SerialTerminal),
        cv.GenerateID(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_PATH, default="/serial"): cv.string,
        cv.Optional(CONF_BUFFER_SIZE, default=512): cv.int_range(min=64, max=4096),
    }
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(
                web_server_base.WebServerBase
            ),
            cv.Optional(CONF_SERIAL_TERMINALS, default=[]): cv.ensure_list(
                SERIAL_TERMINAL_SCHEMA
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on([PLATFORM_ESP32, PLATFORM_ESP8266]),
)


async def to_code(config):
    """Generate code for serial_terminal component."""
    if not config.get(CONF_SERIAL_TERMINALS):
        return
        
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    
    for terminal_config in config[CONF_SERIAL_TERMINALS]:
        uart_component = await cg.get_variable(terminal_config[CONF_UART_ID])
        path = terminal_config[CONF_PATH]
        buffer_size = terminal_config[CONF_BUFFER_SIZE]
        
        # For ESP32, construct with all parameters
        if CORE.is_esp32:
            server_expr = cg.RawExpression(f"{base}->get_server()")
            var = cg.new_Pvariable(
                terminal_config[CONF_ID],
                uart_component,
                path,
                server_expr,
                buffer_size
            )
        else:
            # ESP8266 stub - simpler constructor
            var = cg.new_Pvariable(
                terminal_config[CONF_ID],
                uart_component,
                path
            )
        
        await cg.register_component(var, terminal_config)
    
    cg.add_define("USE_SERIAL_TERMINAL")
