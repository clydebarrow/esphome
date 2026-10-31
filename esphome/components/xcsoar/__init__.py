"""Run the XCSoar glide computer in its own task, drawing on an ESPHome display.

XCSoar itself is built as an ESP-IDF component from a local XCSoar checkout that has the
``esp32/xcsoar`` component. See DESIGN.md for the overall plan.
"""

from dataclasses import dataclass
from pathlib import Path

from esphome import automation
import esphome.codegen as cg
from esphome.components import display, filesystem, psram, touchscreen
from esphome.components.const import BYTE_ORDER_LITTLE, CONF_BYTE_ORDER
from esphome.components.esp32 import (
    VARIANT_ESP32P4,
    add_idf_component,
    add_idf_sdkconfig_option,
    only_on_variant,
    require_vfs_dir,
    require_vfs_select,
)
from esphome.components.mipi import get_color_depth
import esphome.config_validation as cv
from esphome.const import (
    CONF_DISPLAY_ID,
    CONF_FILE,
    CONF_ID,
    CONF_NAME,
    CONF_PLATFORM,
    CONF_RAW_DATA_ID,
    Framework,
)
from esphome.core import CORE, HexInt
import esphome.final_validate as fv
from esphome.types import ConfigType

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["display", "esp32", "psram"]
DOMAIN = "xcsoar"

CONF_DEMO = "demo"
CONF_DPI = "dpi"
CONF_FONTS = "fonts"
CONF_ON_STOPPED = "on_stopped"
CONF_SKYECHO_ID = "skyecho_id"
CONF_WIFI_NETWORKS_ID = "wifi_networks_id"
# The C library file and directory handles XCSoar can have open on its data filesystem
VFS_MAX_FILES = 8
VFS_MAX_DIRS = 4
CONF_FRAME_INTERVAL = "frame_interval"
CONF_TASK_STACK_SIZE = "task_stack_size"
CONF_TOUCHSCREEN_ID = "touchscreen_id"
CONF_XCSOAR_PATH = "xcsoar_path"

# The IDF component inside the XCSoar checkout
IDF_COMPONENT_DIR = Path("esp32", "xcsoar")

# Same key as the mipi_dsi option
CONF_FRAME_BUFFERS = "frame_buffers"
MIPI_DSI_DOMAIN = "mipi_dsi"

xcsoar_ns = cg.esphome_ns.namespace(DOMAIN)
XCSoarComponent = xcsoar_ns.class_("XCSoarComponent", cg.Component)
# Named here rather than imported, so the skyecho component is only loaded by configs that use it
SkyEcho = cg.esphome_ns.namespace("skyecho").class_("SkyEcho", cg.PollingComponent)
WifiNetworks = cg.esphome_ns.namespace("wifi_networks").class_(
    "WifiNetworks", cg.Component
)


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


def _require_data_filesystem(config: ConfigType) -> ConfigType:
    if not config[CONF_DEMO] and filesystem.CONF_FILESYSTEM_ID not in config:
        raise cv.Invalid(
            f"XCSoar keeps its data on a filesystem: set '{filesystem.CONF_FILESYSTEM_ID}', "
            f"or '{CONF_DEMO}: true' for the test scene"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(XCSoarComponent),
            cv.Required(CONF_DISPLAY_ID): cv.use_id(display.Display),
            cv.Required(CONF_XCSOAR_PATH): _validate_xcsoar_path,
            cv.Optional(CONF_TOUCHSCREEN_ID): cv.use_id(touchscreen.Touchscreen),
            cv.Optional(CONF_SKYECHO_ID): cv.use_id(SkyEcho),
            cv.Optional(CONF_WIFI_NETWORKS_ID): cv.use_id(WifiNetworks),
            cv.Optional(filesystem.CONF_FILESYSTEM_ID): cv.use_id(
                filesystem.Filesystem
            ),
            cv.Optional(CONF_FONTS, default=[]): cv.ensure_list(FONT_SCHEMA),
            cv.Optional(CONF_DEMO, default=False): cv.boolean,
            # XCSoar's -dpi range
            cv.Optional(CONF_DPI): cv.int_range(min=32, max=512),
            cv.Optional(
                CONF_FRAME_INTERVAL, default="100ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TASK_STACK_SIZE, default=65536): cv.int_range(min=16384),
            cv.Optional(CONF_ON_STOPPED): automation.validate_automation({}),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_framework(Framework.ESP_IDF),
    only_on_variant(supported=[VARIANT_ESP32P4]),
    _require_data_filesystem,
)


@dataclass
class XCSoarData:
    # The display is a mipi_dsi panel whose frame buffers XCSoar can be shown from directly
    use_frame_buffers: bool = False


def _get_data() -> XCSoarData:
    if DOMAIN not in CORE.data:
        CORE.data[DOMAIN] = XCSoarData()
    return CORE.data[DOMAIN]


def _final_validate(config: ConfigType) -> None:
    if (fs_id := config.get(filesystem.CONF_FILESYSTEM_ID)) is not None:
        filesystem.require_vfs_path(fs_id)
    disp = fv.get_declaring_config(config[CONF_DISPLAY_ID])
    if disp.get(CONF_PLATFORM) != MIPI_DSI_DOMAIN or disp[CONF_FRAME_BUFFERS] < 2:
        return
    # The PPA writes RGB565 and RGB888 in little endian order only
    if (
        get_color_depth(disp) == 16
        and disp.get(CONF_BYTE_ORDER, BYTE_ORDER_LITTLE) != BYTE_ORDER_LITTLE
    ):
        raise cv.Invalid(
            f"{DOMAIN} needs '{CONF_BYTE_ORDER}: {BYTE_ORDER_LITTLE}' on a display with "
            f"'{CONF_FRAME_BUFFERS}'"
        )
    _get_data().use_frame_buffers = True


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config: ConfigType) -> None:
    disp = await cg.get_variable(config[CONF_DISPLAY_ID])
    var = cg.new_Pvariable(config[CONF_ID], disp)
    await cg.register_component(var, config)
    cg.add(var.set_frame_interval(config[CONF_FRAME_INTERVAL].total_milliseconds))
    cg.add(var.set_task_stack_size(config[CONF_TASK_STACK_SIZE]))
    for conf in config.get(CONF_ON_STOPPED, []):
        await automation.build_callback_automation(
            var, "add_on_stopped_callback", [], conf
        )
    if config[CONF_DEMO]:
        cg.add(var.set_demo(True))
    cg.add_optional(var.set_dpi, config.get(CONF_DPI))

    add_idf_component(
        name=DOMAIN, path=str(config[CONF_XCSOAR_PATH] / IDF_COMPONENT_DIR)
    )
    # XCSoar reports errors with exceptions, and the font search uses stat()
    add_idf_sdkconfig_option("CONFIG_COMPILER_CXX_EXCEPTIONS", True)
    # Boost's R-tree (airspace and waypoint lookups) uses dynamic_cast, and fmt the type of std::locale::facet
    add_idf_sdkconfig_option("CONFIG_COMPILER_CXX_RTTI", True)
    # XCSoar is built with NDEBUG, and its asserts name members that only debug builds have
    add_idf_sdkconfig_option("CONFIG_COMPILER_ASSERT_NDEBUG_EVALUATE", False)
    require_vfs_dir()
    # XCSoar's event loop polls an eventfd
    require_vfs_select()
    psram.request_external_task_stack()
    await cg.add_variable(var.set_touchscreen, config.get(CONF_TOUCHSCREEN_ID))
    if await cg.add_variable(var.set_skyecho, config.get(CONF_SKYECHO_ID)) is not None:
        cg.add_define("USE_XCSOAR_SKYECHO")
    if (
        await cg.add_variable(var.set_wifi_networks, config.get(CONF_WIFI_NETWORKS_ID))
        is not None
    ):
        cg.add_define("USE_XCSOAR_WIFI_NETWORKS")
    if (fs_id := config.get(filesystem.CONF_FILESYSTEM_ID)) is not None:
        filesystem.request_vfs_handles(fs_id, VFS_MAX_FILES, VFS_MAX_DIRS)
        await cg.add_variable(var.set_filesystem, fs_id)
    if _get_data().use_frame_buffers:
        cg.add_define("USE_XCSOAR_FRAME_BUFFERS")

    if fonts := config[CONF_FONTS]:
        cg.add_define("XCSOAR_ROM_FILE_COUNT", len(fonts))
    for font in fonts:
        data = font[CONF_FILE].read_bytes()
        array = cg.progmem_array(font[CONF_RAW_DATA_ID], [HexInt(x) for x in data])
        cg.add(var.add_rom_file(f"fonts/{font[CONF_NAME]}", array, len(data)))
