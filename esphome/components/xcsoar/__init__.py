"""Run the XCSoar glide computer in its own task, drawing on an ESPHome display.

XCSoar itself is built as an ESP-IDF component from a local XCSoar checkout that has the
``esp32/xcsoar`` component. See DESIGN.md for the overall plan.
"""

from pathlib import Path

import esphome.codegen as cg
from esphome.components import display, psram
from esphome.components.esp32 import (
    VARIANT_ESP32P4,
    add_idf_component,
    add_idf_sdkconfig_option,
    only_on_variant,
    require_vfs_dir,
)
import esphome.config_validation as cv
from esphome.const import (
    CONF_DISPLAY_ID,
    CONF_FILE,
    CONF_ID,
    CONF_NAME,
    CONF_RAW_DATA_ID,
    Framework,
)
from esphome.core import HexInt
from esphome.types import ConfigType

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["display", "esp32", "psram"]
DOMAIN = "xcsoar"

CONF_FONTS = "fonts"
CONF_FRAME_INTERVAL = "frame_interval"
CONF_TASK_STACK_SIZE = "task_stack_size"
CONF_XCSOAR_PATH = "xcsoar_path"

# The IDF component inside the XCSoar checkout
IDF_COMPONENT_DIR = Path("esp32", "xcsoar")

xcsoar_ns = cg.esphome_ns.namespace(DOMAIN)
XCSoarComponent = xcsoar_ns.class_("XCSoarComponent", cg.Component)


def _validate_xcsoar_path(value: object) -> Path:
    path = cv.directory(value).resolve()
    if not (path / IDF_COMPONENT_DIR / "CMakeLists.txt").is_file():
        raise cv.Invalid(f"{path} is not an XCSoar checkout with {IDF_COMPONENT_DIR}")
    return path


def _font_name(config: ConfigType) -> ConfigType:
    config.setdefault(CONF_NAME, config[CONF_FILE].name)
    return config


FONT_SCHEMA = cv.All(
    cv.maybe_simple_value(
        {
            cv.Required(CONF_FILE): cv.file_,
            cv.Optional(CONF_NAME): cv.All(cv.string_strict, cv.Length(min=1)),
            cv.GenerateID(CONF_RAW_DATA_ID): cv.declare_id(cg.uint8),
        },
        key=CONF_FILE,
    ),
    _font_name,
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(XCSoarComponent),
            cv.Required(CONF_DISPLAY_ID): cv.use_id(display.Display),
            cv.Required(CONF_XCSOAR_PATH): _validate_xcsoar_path,
            cv.Optional(CONF_FONTS, default=[]): cv.ensure_list(FONT_SCHEMA),
            cv.Optional(
                CONF_FRAME_INTERVAL, default="100ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TASK_STACK_SIZE, default=65536): cv.int_range(min=16384),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_framework(Framework.ESP_IDF),
    only_on_variant(supported=[VARIANT_ESP32P4]),
)


async def to_code(config: ConfigType) -> None:
    disp = await cg.get_variable(config[CONF_DISPLAY_ID])
    var = cg.new_Pvariable(config[CONF_ID], disp)
    await cg.register_component(var, config)
    cg.add(var.set_frame_interval(config[CONF_FRAME_INTERVAL].total_milliseconds))
    cg.add(var.set_task_stack_size(config[CONF_TASK_STACK_SIZE]))

    add_idf_component(
        name=DOMAIN, path=str(config[CONF_XCSOAR_PATH] / IDF_COMPONENT_DIR)
    )
    # XCSoar reports errors with exceptions, and the font search uses stat()
    add_idf_sdkconfig_option("CONFIG_COMPILER_CXX_EXCEPTIONS", True)
    require_vfs_dir()
    psram.request_external_task_stack()

    if fonts := config[CONF_FONTS]:
        cg.add_define("XCSOAR_ROM_FILE_COUNT", len(fonts))
    for font in fonts:
        data = font[CONF_FILE].read_bytes()
        array = cg.progmem_array(font[CONF_RAW_DATA_ID], [HexInt(x) for x in data])
        cg.add(var.add_rom_file(f"fonts/{font[CONF_NAME]}", array, len(data)))
