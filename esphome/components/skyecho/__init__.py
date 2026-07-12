import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

DOMAIN = "skyecho"

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["network"]

# Key under CORE.data[DOMAIN] set when the LVGL traffic widget is configured.
KEY_NEEDS_IMAGES = "needs_images"


def AUTO_LOAD(config):
    # image/file are only needed when the LVGL traffic widget bundles its symbols.
    # image depends on display, so it must not be pulled in for display-less setups.
    loads = ["socket", "uart"]
    if CORE.data.get(DOMAIN, {}).get(KEY_NEEDS_IMAGES):
        loads += ["image", "file"]
    return loads


CONF_FLARM_UART = "flarm_uart"

skyecho_ns = cg.esphome_ns.namespace("skyecho")
SkyEchoComponent = skyecho_ns.class_("SkyEcho", cg.PollingComponent)

CONF_SKYECHO_ID = "skyecho_id"


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


# Registers the custom "skyecho_traffic" LVGL widget. Importing here (after the
# definitions above) ensures the widget type is available whenever the skyecho
# component is loaded, before LVGL builds its widget schema.
from . import traffic_widget  # noqa: E402,F401  pylint: disable=wrong-import-position
