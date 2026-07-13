from esphome import automation
import esphome.codegen as cg
from esphome.components import wifi
import esphome.config_validation as cv

from ..defines import CONF_MAIN, CONF_SCROLLBAR, add_lv_use
from ..lvcode import lv_add, lv_expr
from ..types import LvCompound, LvType
from . import Widget, WidgetType

CONF_WIFI_CHOOSER = "wifi_chooser"
CONF_MULTI_SELECT = "multi_select"
CONF_SHOW_RSSI = "show_rssi"
CONF_HIDE_HIDDEN = "hide_hidden"
CONF_AUTO_SCAN = "auto_scan"
CONF_ON_SELECT = "on_select"

CONF_LIST = "list"

# The C++ class lives in the wifi component (esphome/components/wifi/lvgl_wifi_chooser.h)
# since it owns the scan-results data the widget displays; this file only supplies the
# lvgl-side schema and code generation glue that every widget must register through.
lv_wifi_chooser_t = LvType("wifi::WifiChooser", parents=(LvCompound,))

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
    (or, with 'multi_select', several) of them. The list is populated at runtime by the
    wifi component, not from YAML, since the available networks aren't known until the
    device actually scans.
    """

    def __init__(self):
        super().__init__(
            CONF_WIFI_CHOOSER,
            lv_wifi_chooser_t,
            (CONF_MAIN, CONF_SCROLLBAR),
            WIFI_CHOOSER_SCHEMA,
        )

    @property
    def required_component(self):
        return "wifi"

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
