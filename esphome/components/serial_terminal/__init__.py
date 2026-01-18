"""Serial Terminal component for ESPHome web server."""
from __future__ import annotations

import esphome.codegen as cg
from esphome.components import uart, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_UART_ID, PLATFORM_ESP32, PLATFORM_ESP8266
from esphome.core import CORE

CODEOWNERS = ["@esphome/core"]
DEPENDENCIES = ["web_server_base", "uart"]

serial_terminal_ns = cg.esphome_ns.namespace("serial_terminal")
SerialTerminal = serial_terminal_ns.class_("SerialTerminal", cg.Component)

CONF_SERIAL_TERMINALS = "serial_terminals"
CONF_PATH = "path"

SERIAL_TERMINAL_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SerialTerminal),
        cv.GenerateID(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Optional(CONF_PATH, default="/serial"): cv.string,
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
        var = cg.new_Pvariable(terminal_config[CONF_ID])
        await cg.register_component(var, terminal_config)
        
        uart_component = await cg.get_variable(terminal_config[CONF_UART_ID])
        cg.add(var.set_uart(uart_component))
        cg.add(var.set_path(terminal_config[CONF_PATH]))
        
        # For ESP32, set the server handle
        # The WebSocket handler is registered in setup() using httpd_register_uri_handler
        if CORE.is_esp32:
            cg.add(var.set_server(cg.RawExpression(f"{base}->get_server()")))
    
    cg.add_define("USE_SERIAL_TERMINAL")
