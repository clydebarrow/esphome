import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import (
    core,
    pins,
)
from esphome.components import (
    display,
    byte_bus,
)
from esphome.components.display import validate_rotation
from esphome.core import (
    CORE,
    HexInt,
)
from esphome.const import (
    CONF_COLOR_PALETTE,
    CONF_ID,
    CONF_LAMBDA,
    CONF_MODEL,
    CONF_RAW_DATA_ID,
    CONF_PAGES,
    CONF_RESET_PIN,
    CONF_DIMENSIONS,
    CONF_WIDTH,
    CONF_HEIGHT,
    CONF_ROTATION,
    CONF_MIRROR_X,
    CONF_MIRROR_Y,
    CONF_SWAP_XY,
    CONF_COLOR_ORDER,
    CONF_OFFSET_HEIGHT,
    CONF_OFFSET_WIDTH,
    CONF_TRANSFORM,
    CONF_INVERT_COLORS,
    CONF_DATA_RATE,
)

DEPENDENCIES = []

CODEOWNERS = ["@nielsnl68", "@clydebarrow"]

ili9xxx_ns = cg.esphome_ns.namespace("ili9xxx")
ILI9XXXDisplay = ili9xxx_ns.class_(
    "ILI9XXXDisplay",
    cg.PollingComponent,
    display.Display,
    display.DisplayBuffer,
)

ILI9XXXColorMode = ili9xxx_ns.enum("ILI9XXXColorMode")
ColorOrder = display.display_ns.enum("ColorMode")

MODELS = {
    "GC9A01A": [
        ili9xxx_ns.class_("ILI9XXXGC9A01A", ILI9XXXDisplay),
        True,
        [
            "spi",
            "dspi",
            "i80",
            "par8",
            "par9",
            "par12",
            "par16",
            "par18",
            "rgb6",
            "rgb12",
            "rgb16",
            "rgb18",
        ],
        [240, 240],
    ],
    "ILI9341": [
        ili9xxx_ns.class_("ILI9XXXILI9341", ILI9XXXDisplay),
        True,
        [
            "spi",
            "i80",
            "par8",
            "par9",
            "par16",
            "par18",
            "rgb6",
            "rgb16",
            "rgb18",
        ],
        [320, 240],
    ],
    "ILI9342": [
        ili9xxx_ns.class_("ILI9XXXILI9342", ILI9XXXDisplay),
        True,
        [
            "spi",
            "i80",
            "par8",
            "par9",
            "par16",
            "par18",
            "rgb6",
            "rgb16",
            "rgb18",
        ],
        [240, 320],
    ],
    "ILI9342C": [
        ili9xxx_ns.class_("ILI9XXXILI9342", ILI9XXXDisplay),
        True,
        [
            "spi",
            "i80",
            "par8",
            "par9",
            "par16",
            "par18",
            "rgb6",
            "rgb16",
            "rgb18",
        ],
        [240, 320],
    ],
    "ILI9481": [
        ili9xxx_ns.class_("ILI9XXXILI9481", ILI9XXXDisplay),
        True,
        [
            "spi",
            "i80",
            "qspi",
            "par8",
            "par9",
            "par16",
            "par18",
            "rgb16",
            "rgb18",
        ],
        [480, 320],
    ],
    "ILI9486": [
        ili9xxx_ns.class_("ILI9XXXILI9486", ILI9XXXDisplay),
        False,
        [
            "spi",
            "i80",
            "par8",
            "par9",
            "par16",
            "par18",
            "rgb16",
            "rgb18",
        ],
        [480, 320],
    ],
    "ILI9488": [
        ili9xxx_ns.class_("ILI9XXXILI9488", ILI9XXXDisplay),
        False,
        [
            "spi",
            "i80",
            "par8",
            "par9",
            "par16",
            "par18",
            "par24",
            "rgb16",
            "rgb18",
            "rgb24",
        ],
        [480, 320],
    ],
    "ILI9488_A": [
        ili9xxx_ns.class_("ILI9XXXILI9488A", ILI9XXXDisplay),
        False,
        [
            "spi",
            "i80",
            "par8",
            "par9",
            "par16",
            "par18",
            "par24",
            "rgb16",
            "rgb18",
            "rgb24",
        ],
        [480, 320],
    ],
    "ST7796": [
        ili9xxx_ns.class_("ILI9XXXST7796", ILI9XXXDisplay),
        True,
        [
            "spi",
            "i80",
            "par8",
            "par9",
            "par16",
            "par18",
            "rgb16",
            "rgb18",
        ],
        [480, 320],
    ],
    "ST7796S": [
        ili9xxx_ns.class_("ILI9XXXST7796", ILI9XXXDisplay),
        True,
        [
            "spi",
            "i80",
            "par8",
            "par9",
            "par16",
            "par18",
            "rgb16",
            "rgb18",
            "rgb24",
        ],
        [480, 320],
    ],
    "ST7796U": [
        ili9xxx_ns.class_("ILI9XXXST7796", ILI9XXXDisplay),
        False,
        [
            "spi",
            "i80",
            "par8",
            "par9",
            "par16",
            "par18",
            "rgb16",
            "rgb18",
            "rgb24",
        ],
        [480, 320],
    ],
    "ST7789V": [
        ili9xxx_ns.class_("ILI9XXXST7789V", ILI9XXXDisplay),
        True,
        [
            "spi",
            "i80",
            "par8",
            "par9",
            "par16",
            "par18",
            "rgb6",
            "rgb16",
            "rgb18",
            "rgb24",
        ],
        [320, 240],
    ],
    "M5STACK": [ili9xxx_ns.class_("ILI9XXXM5Stack", ILI9XXXDisplay), False, ["spi"]],
    "M5CORE": [ili9xxx_ns.class_("ILI9XXXM5CORE", ILI9XXXDisplay), True, ["spi"]],
    "TFT_2.4": [
        ili9xxx_ns.class_("ILI9XXXILI9341", ILI9XXXDisplay),
        True,
        ["spi", "i80", "rgb"],
    ],
    "TFT_2.4R": [
        ili9xxx_ns.class_("ILI9XXXILI9342", ILI9XXXDisplay),
        True,
        ["spi", "i80", "rgb"],
    ],
    "S3BOX": [ili9xxx_ns.class_("ILI9XXXS3Box", ILI9XXXDisplay), False, ["spi"]],
    "S3BOX_LITE": [
        ili9xxx_ns.class_("ILI9XXXS3BoxLite", ILI9XXXDisplay),
        False,
        ["spi"],
    ],
    "W32-SC01-Plus": [
        ili9xxx_ns.class_("ILI9XXXST7796", ILI9XXXDisplay),
        False,
        ["i80", "par8"],
    ],
    "WAVESHARE_RES_3_5": [
        ili9xxx_ns.class_("WAVESHARERES35", ILI9XXXDisplay),
        False,
        ["spi16d"],
    ],
}

COLOR_ORDERS = {
    "RGB": ColorOrder.COLOR_ORDER_RGB,
    "BGR": ColorOrder.COLOR_ORDER_BGR,
}

COLOR_PALETTE = cv.one_of("NONE", "GRAYSCALE", "IMAGE_ADAPTIVE")

CONF_LED_PIN = "led_pin"
CONF_COLOR_PALETTE_IMAGES = "color_palette_images"
CONF_INVERT_DISPLAY = "invert_display"
CONF_BUS_TYPE = "bus_type"
CONF_BYTE_BUS_ID = "byte_bus_id"


def _validate(config):
    if config.get(CONF_COLOR_PALETTE) == "IMAGE_ADAPTIVE" and not config.get(
        CONF_COLOR_PALETTE_IMAGES
    ):
        raise cv.Invalid(
            "Color palette in IMAGE_ADAPTIVE mode requires at least one 'color_palette_images' entry to generate palette"
        )
    if (
        config.get(CONF_COLOR_PALETTE_IMAGES)
        and config.get(CONF_COLOR_PALETTE) != "IMAGE_ADAPTIVE"
    ):
        raise cv.Invalid(
            "Providing color palette images requires palette mode to be 'IMAGE_ADAPTIVE'"
        )

    model = config.get(CONF_MODEL)
    if CORE.is_esp8266 and not MODELS[model][1]:
        raise cv.Invalid("Selected model can't run on ESP8266; use an ESP32 with PSRAM")
    if config[CONF_BUS_TYPE] not in MODELS[model][2]:
        raise cv.Invalid("Selected bus_type can't be used with selected model")
    return config


def give_default_bus_type(config):
    model = config.get(CONF_MODEL, None)
    if model is None:
        return None
    return MODELS[model][2][0]


BASE_SCHEMA = display.FULL_DISPLAY_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(ILI9XXXDisplay),
        cv.Required(CONF_MODEL): cv.enum(MODELS, upper=True, space="_"),
        cv.Optional(CONF_DIMENSIONS): cv.Any(
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
        cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
        cv.Optional(CONF_LED_PIN): cv.invalid(
            "This property is removed. To use the backlight use proper light component."
        ),
        cv.Optional(CONF_COLOR_PALETTE, default="NONE"): COLOR_PALETTE,
        cv.GenerateID(CONF_RAW_DATA_ID): cv.declare_id(cg.uint8),
        cv.Optional(CONF_COLOR_PALETTE_IMAGES, default=[]): cv.ensure_list(cv.file_),
        cv.Optional(CONF_INVERT_DISPLAY): cv.invalid(
            "'invert_display' has been replaced by 'invert_colors'"
        ),
        cv.Optional(CONF_INVERT_COLORS): cv.boolean,
        cv.Optional(CONF_COLOR_ORDER): cv.one_of(*COLOR_ORDERS.keys(), upper=True),
        cv.Exclusive(CONF_ROTATION, CONF_ROTATION): validate_rotation,
        cv.Exclusive(CONF_TRANSFORM, CONF_ROTATION): cv.Schema(
            {
                cv.Optional(CONF_SWAP_XY, default=False): cv.boolean,
                cv.Optional(CONF_MIRROR_X, default=False): cv.boolean,
                cv.Optional(CONF_MIRROR_Y, default=False): cv.boolean,
            }
        ),
    }
).extend(cv.polling_component_schema("1s"))


CONFIG_SCHEMA = cv.All(
    byte_bus.validate_databus_registry(
        BASE_SCHEMA, default=give_default_bus_type, lower=True
    ),
    cv.has_at_most_one_key(CONF_PAGES, CONF_LAMBDA),
    _validate,
)


FINAL_VALIDATE_SCHEMA = byte_bus.final_validate_databus_schema("Display")


async def to_code(config):
    rhs = MODELS[config[CONF_MODEL]][0].new()
    var = cg.Pvariable(config[CONF_ID], rhs)

    data_rate = int(max(config[CONF_DATA_RATE] / 1e6, 1))
    await display.register_display(var, config)

    bus_client = await byte_bus.register_databus(config)
    cg.add(var.set_bus(bus_client))
    cg.add(var.set_data_rate(data_rate))

    if CONF_COLOR_ORDER in config:
        cg.add(var.set_color_order(COLOR_ORDERS[config[CONF_COLOR_ORDER]]))
    if CONF_TRANSFORM in config:
        transform = config[CONF_TRANSFORM]
        cg.add(var.set_swap_xy(transform[CONF_SWAP_XY]))
        cg.add(var.set_mirror_x(transform[CONF_MIRROR_X]))
        cg.add(var.set_mirror_y(transform[CONF_MIRROR_Y]))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(display.DisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))

    if CONF_RESET_PIN in config:
        reset = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
        cg.add(var.set_reset_pin(reset))

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

    rhs = None
    if config[CONF_COLOR_PALETTE] == "GRAYSCALE":
        cg.add(var.set_buffer_color_mode(ILI9XXXColorMode.BITS_8_INDEXED))
        rhs = []
        for x in range(256):
            rhs.extend([HexInt(x), HexInt(x), HexInt(x)])
    elif config[CONF_COLOR_PALETTE] == "IMAGE_ADAPTIVE":
        cg.add(var.set_buffer_color_mode(ILI9XXXColorMode.BITS_8_INDEXED))
        from PIL import Image

        def load_image(filename):
            path = CORE.relative_config_path(filename)
            try:
                return Image.open(path)
            except Exception as e:
                raise core.EsphomeError(f"Could not load image file {path}: {e}")

        # make a wide horizontal combined image.
        images = [load_image(x) for x in config[CONF_COLOR_PALETTE_IMAGES]]
        total_width = sum(i.width for i in images)
        max_height = max(i.height for i in images)

        ref_image = Image.new("RGB", (total_width, max_height))
        x = 0
        for i in images:
            ref_image.paste(i, (x, 0))
            x = x + i.width

        # reduce the colors on combined image to 256.
        converted = ref_image.convert("P", palette=Image.Palette.ADAPTIVE, colors=256)
        # if you want to verify how the images look use
        # ref_image.save("ref_in.png")
        # converted.save("ref_out.png")
        palette = converted.getpalette()
        assert len(palette) == 256 * 3
        rhs = palette
    else:
        cg.add(var.set_buffer_color_mode(ILI9XXXColorMode.BITS_16))

    if rhs is not None:
        prog_arr = cg.progmem_array(config[CONF_RAW_DATA_ID], rhs)
        cg.add(var.set_palette(prog_arr))

    if CONF_INVERT_COLORS in config:
        cg.add(var.invert_colors(config[CONF_INVERT_COLORS]))
