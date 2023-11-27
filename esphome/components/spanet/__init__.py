import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import (
    CONF_ID,
)

CODEOWNERS = ["@clydebarrow"]

AUTO_LOAD = ["number", "sensor"]

spanet_ns = cg.esphome_ns.namespace("spanet")
Spanet = spanet_ns.class_("Spanet", cg.PollingComponent)

CONFIG_SCHEMA = uart.UART_DEVICE_SCHEMA.extend(
    cv.polling_component_schema("10s")
).extend(
    {
        cv.GenerateID(): cv.declare_id(Spanet),
    }
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "spanet",
    baud_rate=38400,
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity="NONE",
    stop_bits=1,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # cg.add(var.set_max_duration(config[CONF_MAX_DURATION]))
