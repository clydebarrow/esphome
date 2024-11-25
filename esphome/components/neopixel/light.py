import esphome.codegen as cg
from esphome.components import light, spi
from esphome.components.spi import CONF_INTERFACE_INDEX
import esphome.config_validation as cv
from esphome.const import (
    CONF_MOSI_PIN,
    CONF_NUM_LEDS,
    CONF_OUTPUT_ID,
    CONF_RESET_DURATION,
    CONF_RGB_ORDER,
    CONF_SPI_ID,
)
from esphome.core import TimePeriod
from esphome.cpp_generator import TemplateArguments
import esphome.final_validate as fv

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["esp32", "spi"]
DOMAIN = "neopixel"

neopixel_ns = cg.esphome_ns.namespace("neopixel")
NeoPixelLEDStripLightOutput = neopixel_ns.class_(
    "NeoPixelLEDStripLightOutput", light.AddressableLight
)

# Maximum number of bytes that can be sent in one frame
# This number is somewhat arbitrary.

MAX_BUF_SIZE = 16384


def rgb_order(value):
    value = value.upper()
    if (
        not all(x in "RGBW" for x in value)
        or not all(x in value for x in "RGB")
        or len(set(value)) != len(value)
    ):
        raise cv.Invalid(
            "RGB order must have exactly one each of R, G and B and optionally W"
        )
    return value


def check_led_number(config):
    num_leds = config[CONF_NUM_LEDS]
    stride = len(config[CONF_RGB_ORDER])
    if num_leds * stride * 3 > MAX_BUF_SIZE:
        raise cv.Invalid(
            f"Number of LEDs ({num_leds}) is too large, may not exceed {MAX_BUF_SIZE // stride // 3}"
        )
    return config


CONFIG_SCHEMA = cv.All(
    light.ADDRESSABLE_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_SPI_ID): cv.use_id(spi.SPIComponent),
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(NeoPixelLEDStripLightOutput),
            cv.Required(CONF_NUM_LEDS): cv.positive_not_null_int,
            cv.Required(CONF_RGB_ORDER): cv.All(
                cv.string_strict,
                cv.Length(min=3, max=4),
                rgb_order,
            ),
            cv.Optional(CONF_RESET_DURATION, default="50us"): cv.All(
                cv.positive_time_period_microseconds,
                cv.Range(
                    min=TimePeriod(microseconds=6), max=TimePeriod(microseconds=1000)
                ),
            ),
        }
    ),
    check_led_number,
    cv.only_with_esp_idf,
)

FINAL_VALIDATE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SPI_ID): fv.id_declaration_match_schema(
            {
                cv.Required(
                    CONF_MOSI_PIN,
                    msg="Component neopixel requires this spi bus to declare a mosi_pin",
                ): cv.valid,
                cv.Required(
                    CONF_INTERFACE_INDEX,
                    msg="Component neopixel requires this spi bus to use a hardware interface",
                ): cv.valid,
            }
        )
    },
    extra=cv.ALLOW_EXTRA,
)


async def to_code(config):
    num_leds = config[CONF_NUM_LEDS]
    order = config[CONF_RGB_ORDER]
    targs = [
        num_leds,
        order.index("R"),
        order.index("G"),
        order.index("B"),
        order.find("W"),
        len(order),
    ]
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], TemplateArguments(*targs))
    reset_bytes = int(config[CONF_RESET_DURATION].microseconds * 0.4 / 8)
    cg.add(var.set_reset_bytes(reset_bytes))
    cg.add(var.set_data_rate(2_500_000))
    await light.register_light(var, config)
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
