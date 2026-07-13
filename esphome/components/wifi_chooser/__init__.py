from esphome import automation
import esphome.codegen as cg
from esphome.components import wifi
from esphome.components.lvgl.defines import CONF_MAIN, CONF_SCROLLBAR, add_lv_use
from esphome.components.lvgl.lvcode import lv_add, lv_expr
from esphome.components.lvgl.types import LvCompound, LvType
from esphome.components.lvgl.widgets import Widget, WidgetType
import esphome.config_validation as cv

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["wifi", "lvgl"]

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    pass


CONF_WIFI_CHOOSER = "wifi_chooser"
CONF_MULTI_SELECT = "multi_select"
CONF_SHOW_RSSI = "show_rssi"
CONF_HIDE_HIDDEN = "hide_hidden"
CONF_AUTO_SCAN = "auto_scan"
CONF_ON_SELECT = "on_select"

CONF_LIST = "list"

# 'wifi_chooser:' takes no options of its own -- its only job is to be the top-level key
# that makes ESPHome load this component (and so compile in wifi_chooser.h/.cpp) and import
# this module, which registers the 'wifi_chooser' lvgl widget below. Every component's
# module is imported before any component's schema is validated (see MetadataValidationStep
# in esphome/config.py), so this is safe regardless of whether 'wifi_chooser:' or 'lvgl:'
# appears first in the user's YAML.
lv_wifi_chooser_t = LvType("wifi_chooser::WifiChooser", parents=(LvCompound,))

WIFI_CHOOSER_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_MULTI_SELECT, default=False): cv.boolean,
        cv.Optional(CONF_SHOW_RSSI, default=True): cv.boolean,
        cv.Optional(CONF_HIDE_HIDDEN, default=True): cv.boolean,
        cv.Optional(CONF_AUTO_SCAN, default=True): cv.boolean,
        cv.Optional(CONF_ON_SELECT): automation.validate_automation({}),
    }
)


class WifiChooserType(WidgetType):
    """
    Lists the WiFi networks found by the most recent scan and lets the user select one
    (or, with 'multi_select', several) of them. The list is populated at runtime, not
    from YAML, since the available networks aren't known until the device actually scans.
    """

    def __init__(self):
        super().__init__(
            CONF_WIFI_CHOOSER,
            lv_wifi_chooser_t,
            (CONF_MAIN, CONF_SCROLLBAR),
            WIFI_CHOOSER_SCHEMA,
        )

    async def obj_creator(self, parent, config: dict):
        add_lv_use(CONF_LIST)
        return lv_expr.list_create(parent)

    async def to_code(self, w: Widget, config: dict):
        wifi.request_wifi_scan_results_listener()
        wifi.request_wifi_scan_results()
        lv_add(w.var.set_multi_select(config[CONF_MULTI_SELECT]))
        lv_add(w.var.set_show_rssi(config[CONF_SHOW_RSSI]))
        lv_add(w.var.set_hide_hidden(config[CONF_HIDE_HIDDEN]))
        for conf in config.get(CONF_ON_SELECT, []):
            await automation.build_callback_automation(
                w.var,
                "add_on_select_callback",
                [(cg.std_string, "ssid"), (cg.bool_, "selected")],
                conf,
            )
        if config[CONF_AUTO_SCAN]:
            lv_add(w.var.rescan())


wifi_chooser_spec = WifiChooserType()
