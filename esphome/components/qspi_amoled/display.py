import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import spi, display
from esphome.const import (
    CONF_RESET_PIN,
    CONF_OUTPUT,
    CONF_ID,
    CONF_DIMENSIONS,
    CONF_WIDTH,
    CONF_HEIGHT,
    CONF_LAMBDA,
    CONF_BRIGHTNESS,
    CONF_ENABLE_PIN,
)

try:
    from esphome.const import CONF_OFFSET_HEIGHT, CONF_OFFSET_WIDTH, CONF_INVERT_COLORS
except ImportError:
    CONF_OFFSET_HEIGHT = "offset_height"
    CONF_OFFSET_WIDTH = "offset_width"
    CONF_INVERT_COLORS = "invert_colors"

CONF_MIRROR_X = "mirror_x"
CONF_MIRROR_Y = "mirror_y"
CONF_SWAP_XY = "swap_xy"
CONF_COLOR_ORDER = "color_order"
CONF_TRANSFORM = "transform"

DEPENDENCIES = ["spi"]

qspi_amoled_ns = cg.esphome_ns.namespace("qspi_amoled")
QSPI_AMOLED = qspi_amoled_ns.class_(
    "QSPI_AMOLED", display.Display, display.DisplayBuffer, cg.Component, spi.SPIDevice
)
ColorOrder = display.display_ns.enum("ColorMode")

COLOR_ORDERS = {
    "RGB": ColorOrder.COLOR_ORDER_RGB,
    "BGR": ColorOrder.COLOR_ORDER_BGR,
}
DATA_PIN_SCHEMA = pins.gpio_pin_schema(
    {
        CONF_OUTPUT: True,
    },
    internal=True,
)

CONFIG_SCHEMA = cv.All(
    display.FULL_DISPLAY_SCHEMA.extend(
        cv.Schema(
            {
                cv.GenerateID(): cv.declare_id(QSPI_AMOLED),
                cv.Required(CONF_DIMENSIONS): cv.Any(
                    cv.dimensions,
                    cv.Schema(
                        {
                            cv.Required(CONF_WIDTH): cv.int_,
                            cv.Required(CONF_HEIGHT): cv.int_,
                            cv.Optional(CONF_OFFSET_HEIGHT, default=0): cv.int_,
                            cv.Optional(CONF_OFFSET_WIDTH, default=0): cv.int_,
                        }
                    ),
                ),
                cv.Optional(CONF_TRANSFORM): cv.Schema(
                    {
                        cv.Optional(CONF_MIRROR_X, default=False): cv.boolean,
                        cv.Optional(CONF_MIRROR_Y, default=False): cv.boolean,
                        cv.Optional(CONF_SWAP_XY, default=False): cv.boolean,
                    }
                ),
                cv.Optional(CONF_COLOR_ORDER, default="RGB"): cv.one_of(
                    *COLOR_ORDERS.keys(), upper=True
                ),
                cv.Optional(CONF_INVERT_COLORS, default=False): cv.boolean,
                cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
                cv.Optional(CONF_ENABLE_PIN): pins.gpio_output_pin_schema,
                cv.Optional(CONF_BRIGHTNESS, default=0xD0): cv.int_range(
                    0, 0xFF, min_included=True, max_included=True
                ),
            }
        ).extend(
            spi.spi_device_schema(
                cs_pin_required=False,
                default_mode="MODE0",
                default_data_rate=10e6,
                quad=True,
            )
        )
    ),
    cv.only_with_esp_idf,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)
    await spi.register_spi_device(var, config)

    cg.add(var.set_color_mode(COLOR_ORDERS[config[CONF_COLOR_ORDER]]))
    cg.add(var.set_invert_colors(config[CONF_INVERT_COLORS]))
    cg.add(var.set_brightness(config[CONF_BRIGHTNESS]))
    if enable_pin := config.get(CONF_ENABLE_PIN):
        enable = await cg.gpio_pin_expression(enable_pin)
        cg.add(var.set_enable_pin(enable))

    if reset_pin := config.get(CONF_RESET_PIN):
        reset = await cg.gpio_pin_expression(reset_pin)
        cg.add(var.set_reset_pin(reset))

    if transform := config.get(CONF_TRANSFORM):
        cg.add(var.set_mirror_x(transform[CONF_MIRROR_X]))
        cg.add(var.set_mirror_y(transform[CONF_MIRROR_Y]))
        cg.add(var.set_swap_xy(transform[CONF_SWAP_XY]))

    if CONF_DIMENSIONS in config:
        dimensions = config[CONF_DIMENSIONS]
        if isinstance(dimensions, dict):
            cg.add(var.set_dimensions(dimensions[CONF_WIDTH], dimensions[CONF_HEIGHT]))
            cg.add(
                var.set_offsets(
                    dimensions[CONF_OFFSET_WIDTH], dimensions[CONF_OFFSET_HEIGHT]
                )
            )
        else:
            (width, height) = dimensions
            cg.add(var.set_dimensions(width, height))

    if lamb := config.get(CONF_LAMBDA):
        lambda_ = await cg.process_lambda(
            lamb, [(display.DisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))