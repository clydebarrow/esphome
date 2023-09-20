import esphome.codegen as cg
import esphome.core as core
from esphome.components.display import DisplayBuffer
from esphome.components import color
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
)

DOMAIN = "lvgl"
DEPENDENCIES = ["display"]
CODEOWNERS = ["@clydebarrow"]

lvgl_ns = cg.esphome_ns.namespace("lvgl")
LvglComponent = lvgl_ns.class_("LvglComponent", cg.Component)
LvglObj = lvgl_ns.class_("LvglObj")
lv_style_t = cg.global_ns.struct("lv_style_t")

CONF_ALIGNMENT = "alignment"
CONF_LABEL = "label"
CONF_LINE = "line"
CONF_WIDGETS = "widgets"
CONF_DISPLAY_ID = "display_id"
CONF_COLOR_DEPTH = "color_depth"
CONF_BACKGROUND_STYLE = "background_style"
CONF_TEXT = "text"
CONF_STYLES = "styles"

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


# Create a schema from a list of optional properties
def prop_schema(props):
    return cv.Schema({cv.Optional(k): v for k, v in props.items()})


def lv_color(value):
    if isinstance(value, int):
        hex = cv.hex_int(value)
        return f"lv_color_hex({hex})"
    id = cv.use_id(color)(value)
    return f"lv_color_from({id})"


def lv_opa(value):
    return cv.one_of(
        0,
        255,
        "LV_OPA_TRANSP",
        "LV_OPA_COVER",
        "LV_OPA_0",
        "LV_OPA_10",
        "LV_OPA_20",
        "LV_OPA_30",
        "LV_OPA_40",
        "LV_OPA_50",
        "LV_OPA_60",
        "LV_OPA_70",
        "LV_OPA_80",
        "LV_OPA_90",
        "LV_OPA_100",
        upper=True
    )(value)


STYLE_PROPS = {
    "bg_color": lv_color,
    "bg_grad_color": lv_color,
    "bg_opa": lv_opa,
    "bg_grad_dir": cv.one_of("LV_GRAD_DIR", "LV_GRAD_HOR", "LV_GRAD_VER"),
    "line_width": cv.positive_int,
    "line_dash_width": cv.positive_int,
    "line_dash_gap": cv.positive_int,
    "line_rounded": cv.boolean,
    "line_color": lv_color,
}


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
        cv.Optional(CONF_BACKGROUND_STYLE): cv.use_id(lv_style_t),
        cv.Optional(CONF_STYLES): cv.ensure_list(
            cv.Schema(
                {
                    cv.Required(CONF_ID): cv.declare_id(lv_style_t)
                }
            ).extend(prop_schema(STYLE_PROPS))
        ),
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
    core.CORE.add_build_flag("-D\\'_STRINGIFY(x)=_STRINGIFY_(x)\\'")
    core.CORE.add_build_flag("-D\\'_STRINGIFY_(x)=#x\\'")
    core.CORE.add_build_flag("-DLV_CONF_SKIP=1")
    core.CORE.add_build_flag("-DLV_USE_USER_DATA=1")
    core.CORE.add_build_flag("-DLV_TICK_CUSTOM=1")
    core.CORE.add_build_flag(
        "-DLV_TICK_CUSTOM_INCLUDE=\\'_STRINGIFY(esphome/components/lvgl/lvgl_hal.h)\\'"
    )
    core.CORE.add_build_flag("-DLV_TICK_CUSTOM_SYS_TIME_EXPR=\\'(lv_millis())\\'")
    # core.CORE.add_build_flag("-DLV_MEM_CUSTOM=1")
    # core.CORE.add_build_flag("-DLV_MEM_CUSTOM_ALLOC=lv_custom_mem_alloc")
    # core.CORE.add_build_flag("-DLV_MEM_CUSTOM_FREE=lv_custom_mem_free")
    # core.CORE.add_build_flag("-DLV_MEM_CUSTOM_REALLOC=lv_custom_mem_realloc")
    # core.CORE.add_build_flag("-DLV_MEM_CUSTOM_INCLUDE=\\'_STRINGIFY(esphome/components/lvgl/lvgl_hal.h)\\'")
    core.CORE.add_build_flag("-DLV_USE_METER=0")
    core.CORE.add_build_flag(f"-DLV_COLOR_DEPTH={config[CONF_COLOR_DEPTH]}")
    core.CORE.add_build_flag("-I src")
    core.CORE.add_build_flag("-Wenum-conversion")

    display = await cg.get_variable(config[CONF_DISPLAY_ID])
    cg.add_global(lvgl_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_display(display))
    cg.add(cg.RawExpression(f"auto lambda_{config[CONF_ID]} = [=] " + "{"))
    if CONF_STYLES in config:
        print(config[CONF_STYLES])
        for style in config[CONF_STYLES]:
            svar = cg.new_Pvariable(style[CONF_ID])
            cg.add(cg.RawExpression(f"lv_style_init({svar})"))
            for prop in STYLE_PROPS:
                if prop in style:
                    cg.add(cg.RawExpression(f"lv_style_set_{prop}({svar}, {style[prop]})"))
    if CONF_BACKGROUND_STYLE in config:
        color_component = await cg.get_variable(config[CONF_BACKGROUND_STYLE])
        cg.add(cg.RawExpression(f"lv_obj_add_style({color_component}, lv_scr_act(), 0"))
    cg.add(cg.RawExpression("} // end " + f"lambda_{config[CONF_ID]}"))
