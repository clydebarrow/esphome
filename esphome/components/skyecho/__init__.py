import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["network"]

CONF_FLARM_UART = "flarm_uart"

skyecho_ns = cg.esphome_ns.namespace("skyecho")
SkyEchoComponent = skyecho_ns.class_("SkyEcho", cg.PollingComponent)


def validate_uart_optional(config):
    """Only validate UART if it's specified."""
    if CONF_FLARM_UART in config:
        return uart.final_validate_device_schema(
            "skyecho", uart_bus=CONF_FLARM_UART, require_rx=True
        )(config)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SkyEchoComponent),
            cv.Optional(CONF_FLARM_UART): cv.use_id(uart.UARTComponent),
        }
    ).extend(cv.polling_component_schema("1s")),
)

FINAL_VALIDATE_SCHEMA = validate_uart_optional


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if flarm_uart_config := config.get(CONF_FLARM_UART):
        uart_component = await cg.get_variable(flarm_uart_config)
        cg.add(var.set_flarm_uart(uart_component))
