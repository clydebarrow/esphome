import esphome.codegen as cg
import logging
import esphome.core as core
from esphome import automation
from esphome.components.image import Image_
from esphome.components.sensor import Sensor
from esphome.schema_extractors import schema_extractor, SCHEMA_EXTRACT
from esphome.components.display import DisplayBuffer
from esphome.components import color
import esphome.config_validation as cv
from esphome.components.font import Font
from esphome.const import (
    CONF_ID,
    CONF_VALUE,
    CONF_RANGE_FROM,
    CONF_RANGE_TO,
    CONF_COLOR,
    CONF_MIN_VALUE,
    CONF_MAX_VALUE,
    CONF_MODE,
    CONF_WIDTH,
)

DOMAIN = "lvgl"
DEPENDENCIES = ["display"]
CODEOWNERS = ["@clydebarrow"]
LOGGER = logging.getLogger(__name__)

lvgl_ns = cg.esphome_ns.namespace("lvgl")
LvglComponent = lvgl_ns.class_("LvglComponent", cg.Component)
FontEngine = lvgl_ns.class_("FontEngine")
ObjModifyAction = lvgl_ns.class_("ObjModifyAction")
lv_obj_t = cg.global_ns.struct("lv_obj_t")
lv_meter_indicator_t = cg.global_ns.struct("lv_meter_indicator_t")
lv_style_t = cg.global_ns.struct("lv_style_t")
lv_disp_t_ptr = cg.global_ns.struct("lv_disp_t").operator("ptr")
lv_point_t = cg.global_ns.struct("lv_point_t")

CONFS = []

for name in CONFS:
    globals()[name] = f"CONF_{name.upper()}"

CONF_ADJUSTABLE = "adjustable"
CONF_ARC = "arc"
CONF_BACKGROUND_STYLE = "background_style"
CONF_CLEAR_FLAGS = "clear_flags"
CONF_SET_FLAGS = "set_flags"
CONF_INDICATORS = "indicators"
CONF_IMG = "img"
CONF_LINE = "line"
CONF_LINE_WIDTH = "line_width"
CONF_TICKS = "ticks"
CONF_SCALES = "scales"
CONF_R_MOD = "r_mod"
CONF_ROTATION = "rotation"
CONF_CHANGE_RATE = "change_rate"
CONF_COLOR_DEPTH = "color_depth"
CONF_COLOR_START = "color_start"
CONF_COLOR_END = "color_end"
CONF_CRITICAL_VALUE = "critical_value"
CONF_DISPLAY_ID = "display_id"
CONF_FLEX_FLOW = "flex_flow"
CONF_LOCAL = "local"
CONF_METER = "meter"
CONF_LABEL = "label"
CONF_LAYOUT = "layout"
CONF_MAJOR_COLOR = "major_color"
CONF_MAJOR_STRIDE = "major_stride"
CONF_MAJOR_LENGTH = "major_length"
CONF_MAJOR_WIDTH = "major_width"
CONF_POINTS = "points"
CONF_ANGLE_RANGE = "angle_range"
CONF_LABEL_GAP = "label_gap"
CONF_SCALE_LINES = "scale_lines"
CONF_TICK_COLOR = "tick_color"
CONF_TICK_COUNT = "tick_count"
CONF_TICK_LENGTH = "tick_length"
CONF_TICK_WIDTH = "tick_width"
CONF_SRC = "src"
CONF_START_ANGLE = "start_angle"
CONF_END_ANGLE = "end_angle"
CONF_STATES = "states"
CONF_STYLE = "style"
CONF_STYLES = "styles"
CONF_STYLE_DEFINITIONS = "style_definitions"
CONF_STYLE_ID = "style_id"
CONF_TEXT = "text"
CONF_WIDGETS = "widgets"
CONF_PIVOT_X = "pivot_x"
CONF_PIVOT_Y = "pivot_y"
CONF_START_VALUE = "start_value"
CONF_END_VALUE = "end_value"
CONF_DEFAULT = "default"
CONF_BYTE_ORDER = "byte_order"
CONF_LOG_LEVEL = "log_level"

LOG_LEVELS = [
    "TRACE",
    "INFO",
    "WARN",
    "ERROR",
    "USER",
    "NONE",
]
STATES = [
    CONF_DEFAULT,
    "checked",
    "focused",
    "edited",
    "hovered",
    "pressed",
    "disabled",
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

FLEX_FLOWS = [
    "ROW",
    "COLUMN",
    "ROW_WRAP",
    "COLUMN_WRAP",
    "ROW_REVERSE",
    "COLUMN_REVERSE",
    "ROW_WRAP_REVERSE",
    "COLUMN_WRAP_REVERSE",
]

OBJ_FLAGS = [
    "hidden",
    "clickable",
    "click_focusable",
    "checkable",
    "scrollable",
    "scroll_elastic",
    "scroll_momentum",
    "scroll_one",
    "scroll_chain_hor",
    "scroll_chain_ver",
    "scroll_chain",
    "scroll_on_focus",
    "scroll_with_arrow",
    "snappable",
    "press_lock",
    "event_bubble",
    "gesture_bubble",
    "adv_hittest",
    "ignore_layout",
    "floating",
    "overflow_visible",
    "layout_1",
    "layout_2",
    "widget_1",
    "widget_2",
    "user_1",
    "user_2",
    "user_3",
    "user_4",
]

ARC_MODES = ["NORMAL", "REVERSE", "SYMMETRICAL"]

# List of other components used
lvgl_components_required = set()


def requires_component(comp):
    def validator(value):
        lvgl_components_required.add(comp)
        return cv.requires_component(comp)(value)

    return validator


def lv_color(value):
    if isinstance(value, int):
        hexval = cv.hex_int(value)
        return f"lv_color_hex({hexval})"
    color_id = cv.use_id(color)(value)
    return f"lv_color_from({color_id})"


# List the LVGL built-in fonts that are available
LV_FONTS = list(map(lambda size: f"montserrat_{size}", range(12, 50, 2)))

# Record those we actually use
lv_fonts_used = set()


def lv_font(value):
    """Accept either the name of a built-in LVGL font, or the ID of an ESPHome font"""
    global lv_fonts_used
    if value == SCHEMA_EXTRACT:
        return LV_FONTS
    if isinstance(value, str) and value.lower() in LV_FONTS:
        font = cv.one_of(*LV_FONTS, lower=True)(value)
        lv_fonts_used.add(font)
        return "&lv_font_" + font
    font = cv.use_id(Font)(value)
    return f"(new lvgl::FontEngine({font}))->get_lv_font()"


def lv_bool(value):
    if cv.boolean(value):
        return "true"
    return "false"


def lv_one_of(choices, prefix):
    """Allow one of a list of choices, mapped to upper case, and prepend the choice with the prefix.
    It's also permitted to include the prefix in the value"""

    @schema_extractor("lv_one_of")
    def validator(value):
        if value == SCHEMA_EXTRACT:
            return choices
        if value.startswith(prefix):
            return cv.one_of(*list(map(lambda v: prefix + v, choices)), upper=True)(
                value
            )
        return prefix + cv.one_of(*choices, upper=True)(value)

    return validator


def pixels_or_percent(value):
    """A length in one axis - either a number (pixels) or a percentage"""
    if isinstance(value, int):
        return str(cv.int_(value))
    # Will throw an exception if not a percentage.
    return f"lv_pct({int(cv.percentage(value) * 100)})"


def lv_zoom(value):
    value = cv.float_range(0.1, 10.0)(value)
    return int(value * 256)


def lv_angle(value):
    return cv.float_range(0.0, 360.0)(cv.angle(value))


@schema_extractor("lv_size")
def lv_size(value):
    """A size in one axis - one of "size_content", a number (pixels) or a percentage"""
    if value == SCHEMA_EXTRACT:
        return ["size_content", "..%"]
    if isinstance(value, str) and not value.endswith("%"):
        if value.upper() == "SIZE_CONTENT":
            return "LV_SIZE_CONTENT"
        raise cv.Invalid("must be 'size_content', a pixel position or a percentage")
    if isinstance(value, int):
        return str(cv.int_(value))
    # Will throw an exception if not a percentage.
    return f"lv_pct({int(cv.percentage(value) * 100)})"


def opacity(value):
    return int(cv.percentage(value) * 255)


STYLE_PROPS = {
    "align": lv_one_of(ALIGNMENTS, "LV_ALIGN_"),
    "bg_color": lv_color,
    "bg_grad_color": lv_color,
    "bg_opa": opacity,
    "bg_grad_dir": lv_one_of(["NONE", "HOR", "VER"], "LV_GRAD_DIR_"),
    "height": lv_size,
    "line_width": cv.positive_int,
    "line_dash_width": cv.positive_int,
    "line_dash_gap": cv.positive_int,
    "line_rounded": lv_bool,
    "line_color": lv_color,
    "text_color": lv_color,
    "text_font": lv_font,
    "transform_angle": lv_angle,
    "transform_width": pixels_or_percent,
    "transform_height": pixels_or_percent,
    "transform_zoom": lv_zoom,
    "max_height": pixels_or_percent,
    "max_width": pixels_or_percent,
    "min_height": pixels_or_percent,
    "min_width": pixels_or_percent,
    "width": lv_size,
    "x": pixels_or_percent,
    "y": pixels_or_percent,
}

# Create a schema from a list of optional properties
PROP_SCHEMA = cv.Schema({cv.Optional(k): v for k, v in STYLE_PROPS.items()})


def generate_id(base):
    generate_id.counter += 1
    return f"lvgl_{base}_{generate_id.counter}"


generate_id.counter = 0


def cv_int_list(il):
    nl = il.replace(" ", "").split(",")
    return list(map(lambda x: int(x), nl))


def cv_point_list(value):
    if not isinstance(value, list):
        raise cv.Invalid("List of points required")
    values = list(map(cv_int_list, value))
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


def lv_value(value):
    if isinstance(value, int):
        return cv.float_(float(cv.int_(value)))
    if isinstance(value, float):
        return cv.float_(value)
    return cv.templatable(cv.use_id(Sensor))(value)


INDICATOR_SCHEMA = cv.Any(
    {
        cv.Exclusive(CONF_LINE, CONF_INDICATORS): cv.Schema(
            {
                cv.GenerateID(): cv.declare_id(lv_meter_indicator_t),
                cv.Optional(CONF_WIDTH, default=4): lv_size,
                cv.Optional(CONF_COLOR, default=0): lv_color,
                cv.Optional(CONF_R_MOD, default=0): lv_size,
                cv.Optional(CONF_VALUE): lv_value,
            }
        ),
        cv.Exclusive(CONF_IMG, CONF_INDICATORS): cv.All(
            cv.Schema(
                {
                    cv.GenerateID(): cv.declare_id(lv_meter_indicator_t),
                    cv.Optional(CONF_PIVOT_X, default=0): lv_size,
                    cv.Optional(CONF_PIVOT_X, default="50%"): lv_size,
                    cv.Optional(CONF_VALUE): lv_value,
                }
            ),
            requires_component("image"),
        ),
        cv.Exclusive(CONF_ARC, CONF_INDICATORS): cv.Schema(
            {
                cv.GenerateID(): cv.declare_id(lv_meter_indicator_t),
                cv.Optional(CONF_WIDTH, default=4): lv_size,
                cv.Optional(CONF_COLOR, default=0): lv_color,
                cv.Optional(CONF_R_MOD, default=0): lv_size,
                cv.Exclusive(CONF_VALUE, CONF_VALUE): lv_value,
                cv.Exclusive(CONF_START_VALUE, CONF_VALUE): lv_value,
                cv.Optional(CONF_END_VALUE): lv_value,
            }
        ),
        cv.Exclusive(CONF_TICKS, CONF_INDICATORS): cv.Schema(
            {
                cv.GenerateID(): cv.declare_id(lv_meter_indicator_t),
                cv.Optional(CONF_WIDTH, default=4): lv_size,
                cv.Optional(CONF_COLOR_START, default=0): lv_color,
                cv.Optional(CONF_COLOR_END): lv_color,
                cv.Optional(CONF_R_MOD, default=0): lv_size,
                cv.Exclusive(CONF_VALUE, CONF_VALUE): lv_value,
                cv.Exclusive(CONF_START_VALUE, CONF_VALUE): lv_value,
                cv.Optional(CONF_END_VALUE): lv_value,
                cv.Optional(CONF_LOCAL, default=False): lv_bool,
            }
        ),
    }
)

SCALE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_TICK_COUNT, default=12): cv.positive_int,
        cv.Optional(CONF_TICK_WIDTH, default=2): lv_size,
        cv.Optional(CONF_TICK_LENGTH, default=10): lv_size,
        cv.Optional(CONF_TICK_COLOR, default=0x808080): lv_color,
        cv.Optional(CONF_MAJOR_STRIDE, default=3): cv.positive_int,
        cv.Optional(CONF_MAJOR_WIDTH, default=5): lv_size,
        cv.Optional(CONF_MAJOR_LENGTH, default="15%"): lv_size,
        cv.Optional(CONF_MAJOR_COLOR, default=0): lv_color,
        cv.Optional(CONF_LABEL_GAP, default=4): lv_size,
        cv.Optional(CONF_RANGE_FROM, default=0.0): cv.float_,
        cv.Optional(CONF_RANGE_TO, default=100.0): cv.float_,
        cv.Optional(CONF_ANGLE_RANGE, default=270): cv.int_range(0, 360),
        cv.Optional(CONF_ROTATION): lv_angle,
        cv.Optional(CONF_INDICATORS): cv.ensure_list(INDICATOR_SCHEMA),
    }
)

ARC_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_VALUE): lv_value,
        cv.Optional(CONF_MIN_VALUE, default=0.0): cv.float_,
        cv.Optional(CONF_MAX_VALUE, default=100.0): cv.float_,
        cv.Optional(CONF_START_ANGLE, default=135): lv_angle,
        cv.Optional(CONF_END_ANGLE, default=45): lv_angle,
        cv.Optional(CONF_ROTATION, default=0.0): lv_angle,
        cv.Optional(CONF_ADJUSTABLE, default=False): bool,
        cv.Optional(CONF_MODE, default="NORMAL"): lv_one_of(ARC_MODES, "LV_ARC_MODE_"),
        cv.Optional(CONF_CHANGE_RATE, default=720): cv.uint16_t,
    }
)

STYLE_SCHEMA = PROP_SCHEMA.extend(
    {
        cv.Optional(CONF_STYLES): cv.ensure_list(cv.use_id(lv_style_t)),
    }
)
STATE_SCHEMA = cv.Schema({cv.Optional(state): STYLE_SCHEMA for state in STATES})
FLAG_SCHEMA = cv.Schema({cv.Optional(flag): cv.boolean for flag in OBJ_FLAGS})
FLAG_LIST = cv.ensure_list(lv_one_of(OBJ_FLAGS, "LV_OBJ_FLAG_"))

OBJ_SCHEMA = (
    STYLE_SCHEMA.extend(STATE_SCHEMA)
    .extend(FLAG_SCHEMA)
    .extend(
        cv.Schema(
            {
                cv.GenerateID(): cv.declare_id(lv_obj_t),
                cv.Optional(CONF_LAYOUT): lv_one_of(["FLEX", "GRID"], "LV_LAYOUT_"),
                cv.Optional(CONF_FLEX_FLOW, default="ROW_WRAP"): lv_one_of(
                    FLEX_FLOWS, prefix="LV_FLEX_FLOW_"
                ),
                cv.Optional(CONF_SET_FLAGS): FLAG_LIST,
                cv.Optional(CONF_CLEAR_FLAGS): FLAG_LIST,
            }
        )
    )
)

WIDGET_SCHEMA = cv.Any(
    {
        cv.Exclusive(CONF_LABEL, CONF_WIDGETS): OBJ_SCHEMA.extend(
            {cv.Optional(CONF_TEXT): cv.string}
        ),
        cv.Exclusive(CONF_LINE, CONF_WIDGETS): OBJ_SCHEMA.extend(
            {cv.Required(CONF_POINTS): cv_point_list}
        ),
        cv.Exclusive(CONF_ARC, CONF_WIDGETS): OBJ_SCHEMA.extend(ARC_SCHEMA),
        cv.Exclusive(CONF_METER, CONF_WIDGETS): OBJ_SCHEMA.extend(
            {
                cv.Optional(CONF_SCALES): cv.ensure_list(SCALE_SCHEMA),
            }
        ),
        cv.Exclusive(CONF_IMG, CONF_WIDGETS): cv.All(
            OBJ_SCHEMA.extend(
                {cv.Required(CONF_SRC): cv.use_id(Image_)},
            ),
            requires_component("image"),
        ),
    }
)

CONFIG_SCHEMA = cv.COMPONENT_SCHEMA.extend(OBJ_SCHEMA).extend(
    {
        cv.GenerateID(): cv.declare_id(LvglComponent),
        cv.GenerateID(CONF_DISPLAY_ID): cv.use_id(DisplayBuffer),
        cv.Optional(CONF_COLOR_DEPTH, default=8): cv.one_of(1, 8, 16, 32),
        cv.Optional(CONF_LOG_LEVEL, default="WARN"): cv.one_of(*LOG_LEVELS, upper=True),
        cv.Optional(CONF_BYTE_ORDER, default="big_endian"): cv.one_of(
            "big_endian", "little_endian"
        ),
        cv.Optional(CONF_STYLE_DEFINITIONS): cv.ensure_list(
            cv.Schema({cv.Required(CONF_ID): cv.declare_id(lv_style_t)})
            .extend(PROP_SCHEMA)
            .extend(STATE_SCHEMA)
        ),
        cv.Required(CONF_WIDGETS): cv.ensure_list(WIDGET_SCHEMA),
    }
)


# MODIFY_SCHEMA = OBJ_SCHEMA.extend(
#    {
#        cv.Required(CONF_ID): cv.use_id(lv_obj_t),
#    }
# ).extend(
#    cv.Any(
#        {
#            cv.Exclusive(CONF_VALUE): lv_value,
#            cv.Exclusive(CONF_TEXT): lv_value,
#        }
#    ),
# )


async def create_lambda(init):
    return await cg.process_lambda(
        core.Lambda(";\n".join([*init, ""])), [(lv_disp_t_ptr, "lv_disp")]
    )


@automation.register_action("lvgl.obj.modify", ObjModifyAction, MODIFY_SCHEMA)
async def modify_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    lamb = create_lambda(set_obj_properties(obj, config))
    var = cg.new_Pvariable(action_id, template_arg, obj, lamb)


async def styles_to_code(styles):
    """Convert styles to C__ code."""
    init = []
    for style in styles:
        svar = cg.new_Pvariable(style[CONF_ID])
        cg.add(cg.RawExpression(f"lv_style_init({svar})"))
        for prop in STYLE_PROPS:
            if prop in style:
                cg.add(cg.RawExpression(f"lv_style_set_{prop}({svar}, {style[prop]})"))
    return init


lv_uses = set()


def collect_props(config):
    props = {}
    for prop in [*STYLE_PROPS, CONF_STYLES]:
        if prop in config:
            props[prop] = config[prop]
    return props


def set_obj_properties(var, config):
    """Return a list of C++ statements to apply properties to an lv_obj_t"""
    init = []
    # Collect top level properties, merge with default props, gather other state props.
    state_props = {
        state: collect_props(config[state]) if state in config else {}
        for state in STATES
    }
    state_props[CONF_DEFAULT].update(collect_props(config))
    for state, props in state_props.items():
        lv_state = f"LV_STATE_{state.upper()}"
        for prop, value in props.items():
            if prop == CONF_STYLES:
                for style_id in value:
                    init.append(f"lv_obj_add_style({var}, {style_id}, {lv_state})")
            else:
                init.append(f"lv_obj_set_style_{prop}({var}, {value}, {lv_state})")
    if CONF_LAYOUT in config:
        layout = config[CONF_LAYOUT].upper()
        init.append(f"lv_obj_set_layout({var}, {layout})")
        if layout == "LV_LAYOUT_FLEX":
            lv_uses.add("FLEX")
            init.append(f"lv_obj_set_flex_flow({var}, {config[CONF_FLEX_FLOW]})")
        if layout == "LV_LAYOUT_GRID":
            lv_uses.add("GRID")
    return init


async def obj_to_code(t, config, screen):
    """Write object creation code for an object extending lv_obj_t"""
    # print(config)
    init = []
    var = cg.Pvariable(config[CONF_ID], cg.nullptr)
    init.append(f"{var} = lv_{t}_create({screen})")
    init.extend(set_obj_properties(var, config))
    return var, init


async def label_to_code(_, var, label):
    """For a text object, create and set text"""
    if CONF_TEXT in label:
        return [f'lv_label_set_text({var}, "{label[CONF_TEXT]}")']
    return []


# Dict of #defines to provide as build flags
lvgl_defines = {}


def add_define(macro, value="1"):
    if macro in lvgl_defines and lvgl_defines[macro] != value:
        LOGGER.error(
            f"Redefinition of {macro} - was {lvgl_defines[macro]}, now {value}"
        )
    lvgl_defines[macro] = value
    cg.add_build_flag(f"-D\\'{macro}\\'=\\'{value}\\'")


async def img_to_code(_, var, image):
    return [f"lv_img_set_src({var}, lv_img_from({image[CONF_SRC]}))"]


async def line_to_code(lv_component, var, line):
    """For a line object, create and add the points"""
    data = line[CONF_POINTS]
    point_list = data[CONF_POINTS]
    initialiser = cg.RawExpression(
        "{" + ",".join(map(lambda p: "{" + f"{p[0]}, {p[1]}" + "}", point_list)) + "}"
    )
    points = cg.static_const_array(data[CONF_ID], initialiser)
    return [f"lv_line_set_points({var}, {points}, {len(point_list)})"]


async def get_value_lambda(value):
    lamb = "nullptr"
    if isinstance(value, core.Lambda):
        lamb = await cg.process_lambda(value, [], return_type=float)
        value = None
    elif isinstance(value, core.ID):
        lamb = "[] {" f"return {value}->get_state();" "}"
        value = None
    return value, lamb


async def get_end_value(config):
    if CONF_END_VALUE in config:
        value = config[CONF_END_VALUE]
        return await get_value_lambda(value)
    return None, "nullptr"


async def get_start_value(config):
    if CONF_START_VALUE in config:
        value = config[CONF_START_VALUE]
    elif CONF_VALUE in config:
        value = config[CONF_VALUE]
    else:
        return None, "nullptr"
    return await get_value_lambda(value)


async def meter_to_code(lv_component, var, meter):
    """For a meter object, create and set parameters"""

    init = []
    if "METER" not in lv_uses:
        lv_uses.add("METER")
        init.append("lv_meter_scale_t * scale")
    if CONF_SCALES in meter:
        for scale in meter[CONF_SCALES]:
            rotation = 90 + (360 - scale[CONF_ANGLE_RANGE]) / 2
            if CONF_ROTATION in scale:
                rotation = scale[CONF_ROTATION]
            init.extend(
                [
                    f"scale = lv_meter_add_scale({var})",
                    f"lv_meter_set_scale_ticks({var}, scale, {scale[CONF_TICK_COUNT]},"
                    + f"{scale[CONF_TICK_WIDTH]}, {scale[CONF_TICK_LENGTH]}, {scale[CONF_TICK_COLOR]})",
                    f"lv_meter_set_scale_major_ticks({var}, scale, {scale[CONF_MAJOR_STRIDE]},"
                    + f"{scale[CONF_MAJOR_WIDTH]}, {scale[CONF_MAJOR_LENGTH]}, {scale[CONF_MAJOR_COLOR]},"
                    + f"{scale[CONF_LABEL_GAP]})",
                    f"lv_meter_set_scale_range({var}, scale, {scale[CONF_RANGE_FROM]},"
                    + f"{scale[CONF_RANGE_TO]}, {scale[CONF_ANGLE_RANGE]}, {rotation})",
                ]
            )
            if CONF_INDICATORS in scale:
                init.append("lv_meter_indicator_t * indicator")
                for indicator in scale[CONF_INDICATORS]:
                    (t, v) = next(iter(indicator.items()))
                    (start_value, start_lamb) = await get_start_value(v)
                    (end_value, end_lamb) = await get_end_value(v)
                    if t == CONF_LINE:
                        init.append(
                            f"indicator = lv_meter_add_needle_line({var}, scale, {v[CONF_WIDTH]},"
                            + f"{v[CONF_COLOR]}, {v[CONF_R_MOD]})"
                        )
                    if t == CONF_ARC:
                        init.append(
                            f"indicator = lv_meter_add_arc({var}, scale, {v[CONF_WIDTH]},"
                            + f"{v[CONF_COLOR]}, {v[CONF_R_MOD]})"
                        )
                    if t == CONF_TICKS:
                        color_end = v[CONF_COLOR_START]
                        if CONF_COLOR_END in v:
                            color_end = v[CONF_COLOR_END]
                        init.append(
                            f"indicator = lv_meter_add_scale_lines({var}, scale, {v[CONF_COLOR_START]},"
                            + f"{color_end}, {v[CONF_LOCAL]}, {v[CONF_R_MOD]})"
                        )
                    if start_value is not None:
                        init.append(
                            f"lv_meter_set_indicator_start_value({var},indicator, {start_value})"
                        )
                    if end_value is not None:
                        init.append(
                            f"lv_meter_set_indicator_end_value({var},indicator, {end_value})"
                        )
                    if start_lamb != "nullptr" or end_lamb != "nullptr":
                        init.append(
                            f"{lv_component}->add_updater(new Indicator({var}, indicator, {start_lamb}, {end_lamb}))"
                        )

    return init


async def arc_to_code(lv_component, var, arc):
    init = [
        f"lv_arc_set_range({var}, {arc[CONF_MIN_VALUE]}, {arc[CONF_MAX_VALUE]})",
        f"lv_arc_set_bg_angles({var}, {arc[CONF_START_ANGLE]}, {arc[CONF_END_ANGLE]})",
        f"lv_arc_set_rotation({var}, {arc[CONF_ROTATION]})",
        f"lv_arc_set_mode({var}, {arc[CONF_MODE]})",
        f"lv_arc_set_change_rate({var}, {arc[CONF_CHANGE_RATE]})",
        f"lv_arc_set_change_rate({var}, {arc[CONF_CHANGE_RATE]})",
    ]
    (value, lamb) = await get_start_value(arc)
    if value is not None:
        init.append(f"lv_arc_set_value({var}, {value})")
    if lamb != "nullptr":
        init.append(f"{lv_component}->add_updater(new Arc({var}, {lamb})")
    return init


async def widget_to_code(lv_component, widget, screen):
    (t, v) = next(iter(widget.items()))
    (var, init) = await obj_to_code(t, v, screen)
    fun = f"{t}_to_code"
    if fun in globals():
        fun = globals()[fun]
        init.extend(await fun(lv_component, var, v))
    else:
        raise cv.Invalid(f"No handler for widget `{t}'")
    return var, init


async def to_code(config):
    cg.add_library("lvgl/lvgl", "8.3.9")
    for comp in lvgl_components_required:
        add_define(f"LVGL_USES_{comp.upper()}")
    add_define("_STRINGIFY(x)", "_STRINGIFY_(x)")
    add_define("_STRINGIFY_(x)", "#x")
    add_define("LV_CONF_SKIP", "1")
    add_define("LV_USE_USER_DATA", "1")
    add_define("LV_TICK_CUSTOM", "1")
    add_define("LV_USE_LOG", "1")
    add_define(
        "LV_TICK_CUSTOM_INCLUDE", "_STRINGIFY(esphome/components/lvgl/lvgl_hal.h)"
    )
    add_define("LV_TICK_CUSTOM_SYS_TIME_EXPR", "(lv_millis())")
    add_define("LV_MEM_CUSTOM", "1")
    add_define("LV_MEM_CUSTOM_ALLOC", "lv_custom_mem_alloc")
    add_define("LV_MEM_CUSTOM_FREE", "lv_custom_mem_free")
    add_define("LV_MEM_CUSTOM_REALLOC", "lv_custom_mem_realloc")
    add_define(
        "LV_MEM_CUSTOM_INCLUDE", "_STRINGIFY(esphome/components/lvgl/lvgl_hal.h)"
    )

    add_define("LV_LOG_LEVEL", f"LV_LOG_LEVEL_{config[CONF_LOG_LEVEL]}")
    for font in lv_fonts_used:
        add_define(f"LV_FONT_{font.upper()}")
    add_define("LV_COLOR_DEPTH", config[CONF_COLOR_DEPTH])
    if config[CONF_COLOR_DEPTH] == 16:
        add_define(
            "LV_COLOR_16_SWAP", "1" if config[CONF_BYTE_ORDER] == "big_endian" else "0"
        )
    core.CORE.add_build_flag("-Isrc")

    display = await cg.get_variable(config[CONF_DISPLAY_ID])
    cg.add_global(lvgl_ns.using)
    lv_component = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(lv_component, config)
    cg.add(lv_component.set_display(display))
    cg.add(cg.RawExpression("lv_init()"))
    init = []
    if CONF_STYLE_DEFINITIONS in config:
        init.extend(await styles_to_code(config[CONF_STYLE_DEFINITIONS]))
    for widg in config[CONF_WIDGETS]:
        (obj, ext_init) = await widget_to_code(lv_component, widg, "lv_scr_act()")
        init.extend(ext_init)
    init.extend(set_obj_properties("lv_scr_act()", config))

    lamb = await create_lambda(init)
    cg.add(lv_component.set_init_lambda(lamb))
    for use in lv_uses:
        core.CORE.add_build_flag(f"-DLV_USE_{use}=1")
