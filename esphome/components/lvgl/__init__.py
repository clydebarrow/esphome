import esphome.codegen as cg
import esphome.core as core
from esphome.components.display import DisplayBuffer
import esphome.config_validation as cv
from build.lib.esphome.components.display_menu_base import CONF_TEXT
from esphome.const import (
    CONF_ID,
)


DEPENDENCIES = ["display"]
CODEOWNERS = ["@clydebarrow"]

lvgl_ns = cg.esphome_ns.namespace("lvgl")
LvglComponent = lvgl_ns.class_("LvglComponent", cg.Component)
LvglObj = lvgl_ns.class_("LvglObj")

CONF_ALIGNMENT = "alignment"
CONF_LABEL = "label"
CONF_LINE = "line"
CONF_WIDGETS = "widgets"
CONF_DISPLAY_ID = "display_id"
CONF_COLOR_DEPTH = "color_depth"

WIDGET = "widget"

ALIGNMENTS = [
    "TOP_LEFT",
    "TOP_MID",
    "TOP_RIGHT",
    "LEFT_MID",
    "CENTER",
    "RIGHT_MID",
    "BOTTOM_LEFT",
    "BOTTOM_MID",
    "BOTTOM_RIGHT",
    "OUT_LEFT_TOP",
    "OUT_TOP_LEFT",
    "OUT_TOP_MID",
    "OUT_TOP_RIGHT",
    "OUT_RIGHT_TOP",
    "OUT_LEFT_MID",
    "OUT_CENTER",
    "OUT_RIGHT_MID",
    "OUT_LEFT_BOTTOM",
    "OUT_BOTTOM_LEFT",
    "OUT_BOTTOM_MID",
    "OUT_BOTTOM_RIGHT",
    "OUT_RIGHT_BOTTOM",
]


def int_list(il):
    nl = il.replace(" ", "").split(",")
    return list(map(lambda x: int(x), nl))


def point_list(value):
    if not isinstance(value, list):
        raise cv.Invalid("List of points required")
    values = list(map(int_list, value))
    for v in values:
        if (
            not isinstance(v, list)
            or not len(v) == 2
            or not isinstance(v[0], int)
            or not isinstance(v[1], int)
        ):
            raise cv.Invalid("Points must be a list of x,y integer pairs")


OBJ_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LvglObj),
        cv.Optional(CONF_ALIGNMENT, default="top_left"): cv.one_of(ALIGNMENTS),
    }
)
LABEL_SCHEMA = OBJ_SCHEMA.extend(
    {
        cv.Optional(CONF_TEXT): cv.string,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LvglComponent),
        cv.GenerateID(CONF_DISPLAY_ID): cv.use_id(DisplayBuffer),
        cv.Optional(CONF_COLOR_DEPTH, default=8): cv.one_of(1, 8, 16, 32),
        cv.Required(CONF_WIDGETS): (
            cv.ensure_list(
                cv.Any(
                    {
                        # cv.Exclusive(CONF_LABEL, WIDGET) ,
                        cv.Exclusive(CONF_LINE, WIDGET): point_list,
                    }
                )
            )
        ),
    }
)


async def to_code(config):
    cg.add_library("lvgl/lvgl", "8.3.9")
    core.CORE.add_build_flag("-DLV_CONF_SKIP=1")
    core.CORE.add_build_flag("-DLV_USE_METER=0")
    core.CORE.add_build_flag(f"-DLV_COLOR_DEPTH={config[CONF_COLOR_DEPTH]}")
    core.CORE.add_build_flag("-I src")
    core.CORE.add_build_flag("-Wenum-conversion")

    display = await cg.get_variable(config[CONF_DISPLAY_ID])
    cg.add_global(lvgl_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_display(display))
