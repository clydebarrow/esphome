import esphome.codegen as cg
import esphome.core as core
from esphome.schema_extractors import schema_extractor, SCHEMA_EXTRACT
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
lv_obj_t = cg.global_ns.struct("lv_obj_t")
lv_style_t = cg.global_ns.struct("lv_style_t")
lv_disp_t_ptr = cg.global_ns.struct("lv_disp_t").operator("ptr")
lv_point_t = cg.global_ns.struct("lv_point_t")

CONF_BACKGROUND_STYLE = "background_style"
CONF_COLOR_DEPTH = "color_depth"
CONF_DISPLAY_ID = "display_id"
CONF_LABEL = "label"
CONF_LINE = "line"
CONF_POINTS = "points"
CONF_STATES = "states"
CONF_STYLE = "style"
CONF_STYLES = "styles"
CONF_STYLE_ID = "style_id"
CONF_TEXT = "text"
CONF_WIDGETS = "widgets"

STATES = [
    "DEFAULT",
    "CHECKED",
    "FOCUSED",
    "EDITED",
    "HOVERED",
    "PRESSED",
    "DISABLED",
]

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
def prop_schema():
    return cv.Schema({cv.Optional(k): v for k, v in STYLE_PROPS.items()})


def lv_color(value):
    if isinstance(value, int):
        hex = cv.hex_int(value)
        return f"lv_color_hex({hex})"
    id = cv.use_id(color)(value)
    return f"lv_color_from({id})"


def lv_bool(value):
    if cv.boolean(value):
        return "true"
    return "false"


def lv_one_of(list, prefix):
    @schema_extractor("lv_one_of")
    def validator(value):
        if value == SCHEMA_EXTRACT:
            return list
        return prefix + cv.one_of(*list, upper=True)(value)

    return validator


# A position in one axis - either a number (pixels) or a percentage
def lv_pixel_pc():
    """A length in one axis - either a number (pixels) or a percentage"""

    @schema_extractor("lv_position")
    def validator(value):
        if isinstance(value, int):
            return str(cv.int_(value))
        # Will throw an exception if not a percentage.
        return f"lv_pct({int(cv.percentage(value) * 100)})"

    return validator


def lv_zoom(value):
    value = cv.float_range(0.1, 10.0)(value)
    return int(value * 256)


def lv_angle(value):
    value = cv.float_range(-360.0, 360.0)(value)
    return int(value * 10)


def lv_size():
    """A size in one axis - one of "size_content", a number (pixels) or a percentage"""

    @schema_extractor("lv_size")
    def validator(value):
        if value == SCHEMA_EXTRACT:
            return ["content_size", "%"]
        if isinstance(value, str):
            if value.upper() == "SIZE_CONTENT":
                return "LV_SIZE_CONTENT"
            raise cv.Invalid("must be 'size_content', a pixel position or a percentage")
        if isinstance(value, int):
            return str(cv.int_(value))
        # Will throw an exception if not a percentage.
        return f"lv_pct({int(cv.percentage(value) * 100)})"

    return validator


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
        upper=True,
    )(value)


STYLE_PROPS = {
    "align": lv_one_of(ALIGNMENTS, "LV_ALIGN_"),
    "bg_color": lv_color,
    "bg_grad_color": lv_color,
    "bg_opa": lv_opa,
    "bg_grad_dir": lv_one_of(["NONE", "HOR", "VER"], "LV_GRAD_DIR_"),
    "height": lv_size,
    "line_width": cv.positive_int,
    "line_dash_width": cv.positive_int,
    "line_dash_gap": cv.positive_int,
    "line_rounded": lv_bool,
    "line_color": lv_color,
    "text_color": lv_color,
    "transform_angle": lv_angle,
    "transform_width": lv_pixel_pc(),
    "transform_height": lv_pixel_pc(),
    "transform_zoom": lv_zoom,
    "max_height": lv_pixel_pc(),
    "max_width": lv_pixel_pc(),
    "min_height": lv_pixel_pc(),
    "min_width": lv_pixel_pc(),
    "width": lv_size(),
    "x": lv_pixel_pc(),
    "y": lv_pixel_pc(),
}


def generate_id(base):
    generate_id.counter += 1
    return f"lvgl_{base}_{generate_id.counter}"


generate_id.counter = 0


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
    return {
        CONF_ID: cv.declare_id(lv_point_t)(generate_id(CONF_POINTS)),
        CONF_POINTS: values,
    }


STYLE_LIST = cv.ensure_list(
    cv.Required(CONF_STYLE_ID),
    cv.use_id(lv_style_t),
    cv.Required(CONF_STATES),
    cv.ensure_list(cv.one_of(STATES, upper=True)),
)


# Schema for an object style. Can either be a style id, or a list of entries each with style_id: and states:
def style_schema(value):
    if isinstance(value, list) and len(value) != 0:
        return STYLE_LIST(value)
    return {CONF_STYLE_ID: cv.use_id(lv_style_t)(value), CONF_STATES: STATES[0:1]}


OBJ_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(lv_obj_t),
        cv.Optional(CONF_STYLE): style_schema,
    }
).extend(prop_schema())


def widgets():
    return cv.Any(
        {
            cv.Exclusive(CONF_LABEL, CONF_WIDGETS): OBJ_SCHEMA.extend(
                {cv.Optional(CONF_TEXT): cv.string}
            ),
            cv.Exclusive(CONF_LINE, CONF_WIDGETS): OBJ_SCHEMA.extend(
                {cv.Required(CONF_POINTS): point_list}
            ),
        }
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LvglComponent),
        cv.GenerateID(CONF_DISPLAY_ID): cv.use_id(DisplayBuffer),
        cv.Optional(CONF_COLOR_DEPTH, default=8): cv.one_of(1, 8, 16, 32),
        cv.Optional(CONF_BACKGROUND_STYLE): cv.use_id(lv_style_t),
        cv.Optional(CONF_STYLES): cv.ensure_list(
            cv.Schema({cv.Required(CONF_ID): cv.declare_id(lv_style_t)}).extend(
                prop_schema()
            )
        ),
        cv.Required(CONF_WIDGETS): cv.ensure_list(widgets()),
    }
)


async def styles_to_code(styles):
    init = []
    for style in styles:
        svar = cg.new_Pvariable(style[CONF_ID])
        cg.add(cg.RawExpression(f"lv_style_init({svar})"))
        for prop in STYLE_PROPS:
            if prop in style:
                cg.add(cg.RawExpression(f"lv_style_set_{prop}({svar}, {style[prop]})"))
    return init


# Write object creation code for an lv_obj_t
async def obj_to_code(t, config, screen):
    print(config)
    init = []
    var = cg.Pvariable(config[CONF_ID], cg.nullptr)
    init.append(f"{var} = lv_{t}_create({screen})")
    if CONF_STYLE in config:
        style = config[CONF_STYLE]
        states = "|".join(map(lambda s: f"LV_STATE_{s}", style[CONF_STATES]))
        init.append(f"lv_obj_add_style({var}, {style[CONF_STYLE_ID]}, {states})")
    for prop in STYLE_PROPS:
        if prop in config:
            init.append(f"lv_obj_set_style_{prop}({var}, {config[prop]}, 0)")
    return var, init


async def label_to_code(var, label):
    """For a text object, create and set text"""
    if CONF_TEXT in label:
        return [f'lv_label_set_text({var}, "{label[CONF_TEXT]}")']
    return []


async def line_to_code(var, line):
    """For a line object, create and add the points"""
    data = line[CONF_POINTS]
    point_list = data[CONF_POINTS]
    initialiser = cg.RawExpression(
        "{" + ",".join(map(lambda p: "{" + f"{p[0]}, {p[1]}" + "}", point_list)) + "}"
    )
    points = cg.static_const_array(data[CONF_ID], initialiser)
    return [f"lv_line_set_points({var}, {points}, {len(point_list)})"]


async def widget_to_code(widget, screen):
    (t, v) = next(iter(widget.items()))
    (var, init) = await obj_to_code(t, v, screen)
    fun = f"{t}_to_code"
    if fun in globals():
        fun = globals()[fun]
        init.extend(await fun(var, v))
    else:
        raise cv.Invalid(f"No handler for widget `{t}'")
    return init


async def to_code(config):
    cg.add_library("lvgl/lvgl", "8.3.9")
    core.CORE.add_build_flag("-D\\'_STRINGIFY(x)=_STRINGIFY_(x)\\'")
    core.CORE.add_build_flag("-D\\'_STRINGIFY_(x)=#x\\'")
    core.CORE.add_build_flag("-DLV_CONF_SKIP=1")
    core.CORE.add_build_flag("-DLV_USE_USER_DATA=1")
    core.CORE.add_build_flag("-DLV_TICK_CUSTOM=1")
    core.CORE.add_build_flag("-DLV_USE_LOG=1")
    core.CORE.add_build_flag("-DLV_FONT_MONTSERRAT_28=1")
    core.CORE.add_build_flag("-DLV_FONT_DEFAULT=\\'&lv_font_montserrat_28\\'")
    core.CORE.add_build_flag("-DLV_LOG_LEVEL=LV_LOG_LEVEL_INFO")
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
    cg.add(cg.RawExpression("lv_init()"))
    init = []
    if CONF_STYLES in config:
        init.extend(await styles_to_code(config[CONF_STYLES]))
    for widget in config[CONF_WIDGETS]:
        init.extend(await widget_to_code(widget, "lv_scr_act()"))

    if CONF_BACKGROUND_STYLE in config:
        background_style = await cg.get_variable(config[CONF_BACKGROUND_STYLE])
        init.append(f"lv_obj_add_style(lv_scr_act(), {background_style}, 0)")
    lamb = await cg.process_lambda(
        core.Lambda(";\n".join([*init, ""])), [(lv_disp_t_ptr, "lv_disp")]
    )
    cg.add(var.set_init_lambda(lamb))
