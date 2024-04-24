import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import (
    spi,
    time,
)
from esphome.const import (
    CONF_ID,
    CONF_RESET_PIN,
    CONF_BUSY_PIN,
    CONF_INTERRUPT_PIN,
    CONF_TIME_ID,
)

DEPENDENCIES = ["spi", "time"]
CODEOWNERS = ["@clydebarrow"]

MULTI_CONF = True
sx1262_ns = cg.esphome_ns.namespace("sx1262")

sx1262 = sx1262_ns.class_("SX1262Component", cg.PollingComponent, spi.SPIDevice)

CONFIG_SCHEMA = (
    cv.polling_component_schema("500ms")
    .extend(
        {
            cv.GenerateID(CONF_ID): cv.declare_id(sx1262),
            cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            cv.Required(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_INTERRUPT_PIN): pins.gpio_input_pin_schema,
            cv.Required(CONF_BUSY_PIN): pins.gpio_input_pin_schema,
        }
    )
    .extend(
        spi.spi_device_schema(
            cs_pin_required=False, default_data_rate="1MHz", default_mode="MODE0"
        )
    )
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    pin = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
    cg.add(var.set_reset_pin(pin))
    pin = await cg.gpio_pin_expression(config[CONF_INTERRUPT_PIN])
    cg.add(var.set_interrupt_pin(pin))
    pin = await cg.gpio_pin_expression(config[CONF_BUSY_PIN])
    cg.add(var.set_busy_pin(pin))
    time_ = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_clock(time_))
