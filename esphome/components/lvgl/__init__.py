import logging
import esphome.core as core
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components.image import Image_
from .defines import (
    # widgets
    CONF_ARC,
    CONF_BAR,
    CONF_BTN,
    CONF_BTNMATRIX,
    CONF_CANVAS,
    CONF_CHECKBOX,
    CONF_DROPDOWN,
    CONF_IMG,
    CONF_LABEL,
    CONF_LINE,
    CONF_METER,
    CONF_ROLLER,
    CONF_SLIDER,
    CONF_SWITCH,
    CONF_TABLE,
    CONF_TEXTAREA,
    # Parts
    CONF_MAIN,
    CONF_SCROLLBAR,
    CONF_INDICATOR,
    CONF_KNOB,
    CONF_SELECTED,
    CONF_ITEMS,
    CONF_TICKS,
    CONF_CURSOR,
    CONF_TEXTAREA_PLACEHOLDER,
    ALIGNMENTS,
    ARC_MODES,
    BAR_MODES,
    LOG_LEVELS,
    STATES,
    PARTS,
    FLEX_FLOWS,
    OBJ_FLAGS,
)

from esphome.components.sensor import Sensor
from esphome.components.touchscreen import Touchscreen, CONF_TOUCHSCREEN_ID
from esphome.schema_extractors import schema_extractor, SCHEMA_EXTRACT
from esphome.components.display import Display
from esphome.components import color
from esphome.components.font import Font
from esphome.components.rotary_encoder.sensor import RotaryEncoderSensor
from esphome.components.binary_sensor import BinarySensor
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
    CONF_SENSOR,
    CONF_BINARY_SENSOR,
    CONF_GROUP,
    CONF_LENGTH,
    CONF_COUNT,
    CONF_STATE,
    CONF_TRIGGER_ID,
    CONF_TIMEOUT,
)

DOMAIN = "lvgl"
DEPENDENCIES = ["display"]
CODEOWNERS = ["@clydebarrow"]
LOGGER = logging.getLogger(__name__)

lvgl_ns = cg.esphome_ns.namespace("lvgl")
LvglComponent = lvgl_ns.class_("LvglComponent", cg.PollingComponent)
LVTouchListener = lvgl_ns.class_("LVTouchListener")
IdleTrigger = lvgl_ns.class_("IdleTrigger", automation.Trigger.template())
FontEngine = lvgl_ns.class_("FontEngine")
ObjUpdateAction = lvgl_ns.class_("ObjUpdateAction", automation.Action)
LvglCondition = lvgl_ns.class_("LvglCondition", automation.Condition)
PauseAction = lvgl_ns.class_("PauseAction", automation.Action)

# Can't use the native type names here, since ESPHome munges variable names and they conflict
lv_point_t = cg.global_ns.struct("LvPointType")
lv_obj_t = cg.global_ns.struct("LvObjType")
lv_obj_t_ptr = lv_obj_t.operator("ptr")
lv_style_t = cg.global_ns.struct("LvStyleType")
lv_theme_t = cg.global_ns.struct("LvThemeType")
lv_theme_t_ptr = lv_theme_t.operator("ptr")
lv_meter_indicator_t = cg.global_ns.struct("LvMeterIndicatorType")
lv_indicator_t = cg.global_ns.struct("LvMeterIndicatorType")
lv_meter_indicator_t_ptr = lv_meter_indicator_t.operator("ptr")
lv_label_t = cg.MockObjClass("LvLabelType", parents=[lv_obj_t])
lv_meter_t = cg.MockObjClass("LvMeterType", parents=[lv_obj_t])
lv_slider_t = cg.MockObjClass("LvSliderType", parents=[lv_obj_t])
lv_btn_t = cg.MockObjClass("LvBtnType", parents=[lv_obj_t])
lv_checkbox_t = cg.MockObjClass("LvCheckboxType", parents=[lv_obj_t])
lv_line_t = cg.MockObjClass("LvLineType", parents=[lv_obj_t])
lv_img_t = cg.MockObjClass("LvImgType", parents=[lv_obj_t])
lv_arc_t = cg.MockObjClass("LvArcType", parents=[lv_obj_t])
lv_bar_t = cg.MockObjClass("LvBarType", parents=[lv_obj_t])
lv_disp_t_ptr = cg.global_ns.struct("lv_disp_t").operator("ptr")

CONF_ADJUSTABLE = "adjustable"
CONF_ANGLE_RANGE = "angle_range"
CONF_ANIMATED = "animated"
CONF_BACKGROUND_STYLE = "background_style"
CONF_BYTE_ORDER = "byte_order"
CONF_CHANGE_RATE = "change_rate"
CONF_COLOR_DEPTH = "color_depth"
CONF_COLOR_END = "color_end"
CONF_COLOR_START = "color_start"
CONF_CRITICAL_VALUE = "critical_value"
CONF_DEFAULT = "default"
CONF_DISPLAY_ID = "display_id"
CONF_END_ANGLE = "end_angle"
CONF_END_VALUE = "end_value"
CONF_FLAGS = "flags"
CONF_FLEX_FLOW = "flex_flow"
CONF_INDICATORS = "indicators"
CONF_LABEL_GAP = "label_gap"
CONF_LAYOUT = "layout"
CONF_LINE_WIDTH = "line_width"
CONF_LOCAL = "local"
CONF_LOG_LEVEL = "log_level"
CONF_LVGL_COMPONENT = "lvgl_component"
CONF_LVGL_ID = "lvgl_id"
CONF_MAJOR = "major"
CONF_OBJ = "obj"
CONF_OBJ_ID = "obj_id"
CONF_ON_IDLE = "on_idle"
CONF_PIVOT_X = "pivot_x"
CONF_PIVOT_Y = "pivot_y"
CONF_POINTS = "points"
CONF_ROTARY_ENCODERS = "rotary_encoders"
CONF_ROTATION = "rotation"
CONF_R_MOD = "r_mod"
CONF_SCALES = "scales"
CONF_SCALE_LINES = "scale_lines"
CONF_SRC = "src"
CONF_START_ANGLE = "start_angle"
CONF_START_VALUE = "start_value"
CONF_STATES = "states"
CONF_STRIDE = "stride"
CONF_STYLE = "style"
CONF_STYLES = "styles"
CONF_STYLE_DEFINITIONS = "style_definitions"
CONF_STYLE_ID = "style_id"
CONF_TEXT = "text"
CONF_THEME = "theme"
CONF_TOUCHSCREENS = "touchscreens"
CONF_WIDGETS = "widgets"


CONF_LVGL_ID = "lvgl_id"
CONF_TITLE = "title"
CONF_CLOSE_BUTTON = "close_button"
CONF_MESSAGE = "message"
CONF_BUTTONS = "buttons"
CONF_ON_RETURN = "on_return"


# list of widgets and the parts allowed
WIDGET_TYPES = {
    CONF_ARC: (CONF_MAIN, CONF_INDICATOR, CONF_KNOB),
    CONF_BTN: (CONF_MAIN,),
    CONF_BAR: (CONF_MAIN, CONF_INDICATOR),
    CONF_BTNMATRIX: (CONF_MAIN, CONF_ITEMS),
    CONF_CANVAS: (CONF_MAIN,),
    CONF_CHECKBOX: (CONF_MAIN, CONF_INDICATOR),
    CONF_DROPDOWN: (CONF_MAIN, CONF_INDICATOR),
    CONF_IMG: (CONF_MAIN,),
    CONF_INDICATOR: (),
    CONF_LABEL: (CONF_MAIN, CONF_SCROLLBAR, CONF_SELECTED),
    CONF_LINE: (CONF_MAIN,),
    CONF_METER: (CONF_MAIN,),
    CONF_OBJ: (CONF_MAIN,),
    CONF_ROLLER: (CONF_MAIN, CONF_SELECTED),
    CONF_SLIDER: (CONF_MAIN, CONF_INDICATOR, CONF_KNOB),
    CONF_SWITCH: (CONF_MAIN, CONF_INDICATOR, CONF_KNOB),
    CONF_TABLE: (CONF_MAIN, CONF_ITEMS),
    CONF_TEXTAREA: (
        CONF_MAIN,
        CONF_SCROLLBAR,
        CONF_SELECTED,
        CONF_CURSOR,
        CONF_TEXTAREA_PLACEHOLDER,
    ),
}
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
LV_FONTS = list(map(lambda size: f"montserrat_{size}", range(12, 50, 2))) + [
    "montserrat_12_subpx",
    "montserrat_28_compressed",
    "dejavu_16_persian_hebrew",
    "simsun_16_cjk16",
    "unscii_8",
    "unscii_16",
]

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
    lv_uses.add("FONT")
    font = cv.use_id(Font)(value)
    return f"(new lvgl::FontEngine({font}))->get_lv_font()"


def lv_bool(value):
    if cv.boolean(value):
        return "true"
    return "false"


def lv_prefix(value, choices, prefix):
    if value.startswith(prefix):
        return cv.one_of(*list(map(lambda v: prefix + v, choices)), upper=True)(value)
    return prefix + cv.one_of(*choices, upper=True)(value)


def lv_animated(value):
    if isinstance(value, bool):
        value = "ON" if value else "OFF"
    return lv_one_of(["OFF", "ON"], "LV_ANIM_")(value)


def lv_one_of(choices, prefix):
    """Allow one of a list of choices, mapped to upper case, and prepend the choice with the prefix.
    It's also permitted to include the prefix in the value"""

    @schema_extractor("one_of")
    def validator(value):
        if value == SCHEMA_EXTRACT:
            return choices
        return lv_prefix(value, choices, prefix)

    return validator


def lv_any_of(choices, prefix):
    """Allow any of a list of choices, mapped to upper case, and prepend the choice with the prefix.
    It's also permitted to include the prefix in the value"""

    @schema_extractor("one_of")
    def validator(value):
        if value == SCHEMA_EXTRACT:
            return choices
        return "|".join(
            map(
                lambda v: "(int)" + lv_prefix(v, choices, prefix), cv.ensure_list(value)
            )
        )

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


@schema_extractor("one_of")
def lv_size(value):
    """A size in one axis - one of "size_content", a number (pixels) or a percentage"""
    if value == SCHEMA_EXTRACT:
        return ["size_content", "pixels", "..%"]
    if isinstance(value, str) and not value.endswith("%"):
        if value.upper() == "SIZE_CONTENT":
            return "LV_SIZE_CONTENT"
        raise cv.Invalid("must be 'size_content', a pixel position or a percentage")
    if isinstance(value, int):
        return str(cv.int_(value))
    # Will throw an exception if not a percentage.
    return f"lv_pct({int(cv.percentage(value) * 100)})"


def lv_opacity(value):
    value = cv.Any(cv.percentage, lv_one_of(["TRANSP", "COVER"], "LV_OPA_"))(value)
    if isinstance(value, str):
        return value
    return int(value * 255)


def lv_stop_value(value):
    return cv.int_range(0, 255)


STYLE_PROPS = {
    "align": lv_one_of(ALIGNMENTS, "LV_ALIGN_"),
    "arc_opa": lv_opacity,
    "arc_color": lv_color,
    "arc_rounded": lv_bool,
    "arc_width": cv.positive_int,
    "bg_color": lv_color,
    "bg_grad_color": lv_color,
    "bg_dither_mode": lv_one_of(["NONE", "ORDERED", "ERR_DIFF"], "LV_DITHER_"),
    "bg_grad_dir": lv_one_of(["NONE", "HOR", "VER"], "LV_GRAD_DIR_"),
    "bg_grad_stop": lv_stop_value,
    "bg_img_opa": lv_opacity,
    "bg_img_recolor": lv_color,
    "bg_img_recolor_opa": lv_opacity,
    "bg_main_stop": lv_stop_value,
    "bg_opa": lv_opacity,
    "border_color": lv_color,
    "border_opa": lv_opacity,
    "border_post": cv.boolean,
    "border_side": lv_any_of(
        ["NONE", "TOP", "BOTTOM", "LEFT", "RIGHT", "INTERNAL"], "LV_BORDER_SIDE_"
    ),
    "border_width": cv.positive_int,
    "clip_corner": lv_bool,
    "height": lv_size,
    "line_width": cv.positive_int,
    "line_dash_width": cv.positive_int,
    "line_dash_gap": cv.positive_int,
    "line_rounded": lv_bool,
    "line_color": lv_color,
    "opa": lv_opacity,
    "opa_layered": lv_opacity,
    "outline_color": lv_color,
    "outline_opa": lv_opacity,
    "outline_pad": cv.positive_int,
    "outline_width": cv.positive_int,
    "pad_all": cv.positive_int,
    "pad_bottom": cv.positive_int,
    "pad_column": cv.positive_int,
    "pad_left": cv.positive_int,
    "pad_right": cv.positive_int,
    "pad_row": cv.positive_int,
    "pad_top": cv.positive_int,
    "shadow_color": lv_color,
    "shadow_ofs_x": cv.int_,
    "shadow_ofs_y": cv.int_,
    "shadow_opa": lv_opacity,
    "shadow_spread": cv.int_,
    "shadow_width": cv.positive_int,
    "text_align": lv_one_of(["LEFT", "CENTER", "RIGHT", "AUTO"], "LV_TEXT_ALIGN_"),
    "text_color": lv_color,
    "text_decor": lv_any_of(["NONE", "UNDERLINE", "STRIKETHROUGH"], "LV_TEXT_DECOR_"),
    "text_font": lv_font,
    "text_letter_space": cv.positive_int,
    "text_line_space": cv.positive_int,
    "text_opa": lv_opacity,
    "transform_angle": lv_angle,
    "transform_height": pixels_or_percent,
    "transform_pivot_x": pixels_or_percent,
    "transform_pivot_y": pixels_or_percent,
    "transform_zoom": lv_zoom,
    "translate_x": pixels_or_percent,
    "translate_y": pixels_or_percent,
    "max_height": pixels_or_percent,
    "max_width": pixels_or_percent,
    "min_height": pixels_or_percent,
    "min_width": pixels_or_percent,
    "radius": cv.Any(cv.positive_int, lv_one_of(["CIRCLE"], "LV_RADIUS_")),
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


def container_schema(widget_type):
    lv_type = globals()[f"lv_{widget_type}_t"]
    schema = obj_schema(widget_type)
    if extras := globals().get(f"{widget_type.upper()}_SCHEMA"):
        schema = schema.extend(extras).add_extra(validate_max_min)
    schema = schema.extend({cv.GenerateID(): cv.declare_id(lv_type)})
    """Delayed evaluation for recursion"""

    def validator(value):
        widgets = cv.Schema(
            {
                cv.Optional(CONF_WIDGETS): cv.ensure_list(WIDGET_SCHEMA),
            }
        )
        return schema.extend(widgets)(value)

    return validator


def validate_max_min(config):
    if CONF_MAX_VALUE in config and CONF_MIN_VALUE in config:
        if config[CONF_MAX_VALUE] <= config[CONF_MIN_VALUE]:
            raise cv.Invalid("max_value must be greater than min_value")
    return config


def lv_value(value):
    if isinstance(value, int):
        return cv.float_(float(cv.int_(value)))
    if isinstance(value, float):
        return cv.float_(value)
    return cv.templatable(cv.use_id(Sensor))(value)


def lv_text_value(value):
    if isinstance(value, cv.Lambda):
        return cv.returning_lambda(value)
    if isinstance(value, core.ID):
        return cv.use_id(Sensor)(value)
    return cv.string(value)


def lv_boolean_value(value):
    if isinstance(value, cv.Lambda):
        return cv.returning_lambda(value)
    if isinstance(value, core.ID):
        return cv.use_id(BinarySensor)(value)
    return cv.boolean(value)


INDICATOR_LINE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_WIDTH, default=4): lv_size,
        cv.Optional(CONF_COLOR, default=0): lv_color,
        cv.Optional(CONF_R_MOD, default=0): lv_size,
        cv.Optional(CONF_VALUE): lv_value,
    }
)
INDICATOR_IMG_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PIVOT_X, default="50%"): lv_size,
        cv.Optional(CONF_PIVOT_Y, default="50%"): lv_size,
        cv.Optional(CONF_VALUE): lv_value,
    }
)
INDICATOR_ARC_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_WIDTH, default=4): lv_size,
        cv.Optional(CONF_COLOR, default=0): lv_color,
        cv.Optional(CONF_R_MOD, default=0): lv_size,
        cv.Exclusive(CONF_VALUE, CONF_VALUE): lv_value,
        cv.Exclusive(CONF_START_VALUE, CONF_VALUE): lv_value,
        cv.Optional(CONF_END_VALUE): lv_value,
    }
)
INDICATOR_TICKS_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_WIDTH, default=4): lv_size,
        cv.Optional(CONF_COLOR_START, default=0): lv_color,
        cv.Optional(CONF_COLOR_END): lv_color,
        cv.Optional(CONF_R_MOD, default=0): lv_size,
        cv.Exclusive(CONF_VALUE, CONF_VALUE): lv_value,
        cv.Exclusive(CONF_START_VALUE, CONF_VALUE): lv_value,
        cv.Optional(CONF_END_VALUE): lv_value,
        cv.Optional(CONF_LOCAL, default=False): lv_bool,
    }
)
INDICATOR_SCHEMA = cv.Schema(
    {
        cv.Exclusive(CONF_LINE, CONF_INDICATORS): INDICATOR_LINE_SCHEMA.extend(
            {
                cv.GenerateID(): cv.declare_id(lv_meter_indicator_t),
            }
        ),
        cv.Exclusive(CONF_IMG, CONF_INDICATORS): cv.All(
            INDICATOR_IMG_SCHEMA.extend(
                {
                    cv.GenerateID(): cv.declare_id(lv_meter_indicator_t),
                }
            ),
            requires_component("image"),
        ),
        cv.Exclusive(CONF_ARC, CONF_INDICATORS): INDICATOR_ARC_SCHEMA.extend(
            {
                cv.GenerateID(): cv.declare_id(lv_meter_indicator_t),
            }
        ),
        cv.Exclusive(CONF_TICKS, CONF_INDICATORS): INDICATOR_TICKS_SCHEMA.extend(
            {
                cv.GenerateID(): cv.declare_id(lv_meter_indicator_t),
            }
        ),
    }
)

SCALE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_TICKS): cv.Schema(
            {
                cv.Optional(CONF_COUNT, default=12): cv.positive_int,
                cv.Optional(CONF_WIDTH, default=2): lv_size,
                cv.Optional(CONF_LENGTH, default=10): lv_size,
                cv.Optional(CONF_COLOR, default=0x808080): lv_color,
                cv.Optional(CONF_MAJOR): cv.Schema(
                    {
                        cv.Optional(CONF_STRIDE, default=3): cv.positive_int,
                        cv.Optional(CONF_WIDTH, default=5): lv_size,
                        cv.Optional(CONF_LENGTH, default="15%"): lv_size,
                        cv.Optional(CONF_COLOR, default=0): lv_color,
                        cv.Optional(CONF_LABEL_GAP, default=4): lv_size,
                    }
                ),
            }
        ),
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
        cv.Optional(CONF_MIN_VALUE, default=0): cv.int_,
        cv.Optional(CONF_MAX_VALUE, default=100): cv.int_,
        cv.Optional(CONF_START_ANGLE, default=135): lv_angle,
        cv.Optional(CONF_END_ANGLE, default=45): lv_angle,
        cv.Optional(CONF_ROTATION, default=0.0): lv_angle,
        cv.Optional(CONF_ADJUSTABLE, default=False): bool,
        cv.Optional(CONF_MODE, default="NORMAL"): lv_one_of(ARC_MODES, "LV_ARC_MODE_"),
        cv.Optional(CONF_CHANGE_RATE, default=720): cv.uint16_t,
    }
)

BAR_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_VALUE): lv_value,
        cv.Optional(CONF_MIN_VALUE, default=0): cv.int_,
        cv.Optional(CONF_MAX_VALUE, default=100): cv.int_,
        cv.Optional(CONF_MODE, default="NORMAL"): lv_one_of(BAR_MODES, "LV_BAR_MODE_"),
        cv.Optional(CONF_ANIMATED, default=True): lv_animated,
    }
)

SLIDER_SCHEMA = BAR_SCHEMA

STYLE_SCHEMA = PROP_SCHEMA.extend(
    {
        cv.Optional(CONF_STYLES): cv.ensure_list(cv.use_id(lv_style_t)),
    }
)
STATE_SCHEMA = cv.Schema({cv.Optional(state): STYLE_SCHEMA for state in STATES}).extend(
    STYLE_SCHEMA
)
SET_STATE_SCHEMA = cv.Schema({cv.Optional(state): cv.boolean for state in STATES})
FLAG_SCHEMA = cv.Schema({cv.Optional(flag): cv.boolean for flag in OBJ_FLAGS})
FLAG_LIST = cv.ensure_list(lv_one_of(OBJ_FLAGS, "LV_OBJ_FLAG_"))


def part_schema(parts):
    if isinstance(parts, str):
        parts = WIDGET_TYPES[parts]
    return cv.Schema({cv.Optional(part): STATE_SCHEMA for part in parts}).extend(
        STATE_SCHEMA
    )


def obj_schema(parts=(CONF_MAIN,)):
    return (
        part_schema(parts)
        .extend(FLAG_SCHEMA)
        .extend(
            cv.Schema(
                {
                    cv.Optional(CONF_LAYOUT): lv_one_of(["FLEX", "GRID"], "LV_LAYOUT_"),
                    cv.Optional(CONF_FLEX_FLOW, default="ROW_WRAP"): lv_one_of(
                        FLEX_FLOWS, prefix="LV_FLEX_FLOW_"
                    ),
                    cv.Optional(CONF_STATE): SET_STATE_SCHEMA,
                    cv.Optional(CONF_GROUP): cv.validate_id_name,
                }
            )
        )
    )


LABEL_SCHEMA = {cv.Optional(CONF_TEXT): lv_text_value}
CHECKBOX_SCHEMA = {cv.Optional(CONF_TEXT): lv_text_value}
LINE_SCHEMA = {cv.Optional(CONF_POINTS): cv_point_list}
METER_SCHEMA = {cv.Optional(CONF_SCALES): cv.ensure_list(SCALE_SCHEMA)}
IMG_SCHEMA = {cv.Required(CONF_SRC): cv.use_id(Image_)}
WIDGET_SCHEMA = cv.Any(
    {
        cv.Exclusive(CONF_ARC, CONF_WIDGETS): container_schema(CONF_ARC),
        cv.Exclusive(CONF_BAR, CONF_WIDGETS): container_schema(CONF_BAR),
        cv.Exclusive(CONF_BTN, CONF_WIDGETS): container_schema(CONF_BTN),
        cv.Exclusive(CONF_IMG, CONF_WIDGETS): cv.All(
            container_schema(CONF_IMG),
            requires_component("image"),
        ),
        cv.Exclusive(CONF_LABEL, CONF_WIDGETS): container_schema(CONF_LABEL),
        cv.Exclusive(CONF_CHECKBOX, CONF_WIDGETS): container_schema(CONF_CHECKBOX),
        cv.Exclusive(CONF_LINE, CONF_WIDGETS): container_schema(CONF_LINE),
        cv.Exclusive(CONF_OBJ, CONF_WIDGETS): container_schema(CONF_OBJ),
        cv.Exclusive(CONF_METER, CONF_WIDGETS): container_schema(CONF_METER),
        cv.Exclusive(CONF_SLIDER, CONF_WIDGETS): container_schema(CONF_SLIDER),
    }
)

CONFIG_SCHEMA = (
    cv.polling_component_schema("1s")
    .extend(obj_schema())
    .extend(
        {
            cv.Optional(CONF_ID, default=CONF_LVGL_COMPONENT): cv.declare_id(
                LvglComponent
            ),
            cv.GenerateID(CONF_DISPLAY_ID): cv.use_id(Display),
            cv.Optional(CONF_TOUCHSCREENS): cv.ensure_list(
                cv.maybe_simple_value(
                    {
                        cv.Required(CONF_TOUCHSCREEN_ID): cv.use_id(Touchscreen),
                        cv.GenerateID(CONF_ID): cv.declare_id(LVTouchListener),
                    },
                    key=CONF_TOUCHSCREEN_ID,
                )
            ),
            cv.Optional(CONF_ROTARY_ENCODERS): cv.All(
                cv.ensure_list(
                    cv.Schema(
                        {
                            cv.Required(CONF_SENSOR): cv.use_id(RotaryEncoderSensor),
                            cv.Optional(CONF_BINARY_SENSOR): cv.use_id(BinarySensor),
                            cv.Optional(CONF_GROUP): cv.validate_id_name,
                        }
                    )
                ),
                requires_component("rotary_encoder"),
            ),
            cv.Optional(CONF_COLOR_DEPTH, default=8): cv.one_of(1, 8, 16, 32),
            cv.Optional(CONF_LOG_LEVEL, default="WARN"): cv.one_of(
                *LOG_LEVELS, upper=True
            ),
            cv.Optional(CONF_BYTE_ORDER, default="big_endian"): cv.one_of(
                "big_endian", "little_endian"
            ),
            cv.Optional(CONF_STYLE_DEFINITIONS): cv.ensure_list(
                cv.Schema({cv.Required(CONF_ID): cv.declare_id(lv_style_t)}).extend(
                    STYLE_SCHEMA
                )
            ),
            cv.Optional(CONF_ON_IDLE): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(IdleTrigger),
                    cv.Required(CONF_TIMEOUT): cv.positive_time_period_seconds,
                }
            ),
            cv.Required(CONF_WIDGETS): cv.ensure_list(WIDGET_SCHEMA),
            cv.Optional(CONF_THEME): cv.Schema(
                dict(
                    map(
                        lambda w: (
                            cv.Optional(w),
                            obj_schema(w),
                        ),
                        WIDGET_TYPES,
                    )
                )
            ).extend({cv.GenerateID(CONF_ID): cv.declare_id(lv_theme_t)}),
        }
    )
)

# For use by platform components
LVGL_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
    }
)


async def add_init_lambda(lv_component, init):
    lamb = await cg.process_lambda(
        core.Lambda(";\n".join([*init, ""])), [(lv_disp_t_ptr, "lv_disp")]
    )
    cg.add(lv_component.add_init_lambda(lamb))
    lv_temp_vars.clear()


EVENT_LAMB = "event_lamb__"


def set_event_cb(obj, code, *varargs):
    init = add_temp_var("event_callback_t", EVENT_LAMB)
    init.extend([f"{EVENT_LAMB} = [](lv_event_t *e) {{ {code} ;}} \n"])
    for arg in varargs:
        init.extend(
            [
                f"lv_obj_add_event_cb({obj}, {EVENT_LAMB}, {arg}, nullptr)",
            ]
        )
    return init


def cgen(*args):
    cg.add(cg.RawExpression("\n".join(args)))


def styles_to_code(styles):
    """Convert styles to C__ code."""
    for style in styles:
        svar = cg.new_Pvariable(style[CONF_ID])
        cgen(f"lv_style_init({svar})")
        for prop in STYLE_PROPS:
            if prop in style:
                cgen(f"lv_style_set_{prop}({svar}, {style[prop]})")


async def theme_to_code(theme):
    tvar = cg.new_Pvariable(theme[CONF_ID])
    lamb = []
    for widget, style in theme.items():
        if not isinstance(style, dict) or widget == CONF_OBJ:
            continue
        lamb.append(f"  if (lv_obj_check_type(obj, &lv_{widget}_class)) {{")
        lamb.extend(set_obj_properties("obj", style))
        lamb.append("  return")
        lamb.append("  }")
    if style := theme.get(CONF_OBJ):
        lamb.extend(set_obj_properties("obj", style))
    lamb = await cg.process_lambda(
        core.Lambda(";\n".join([*lamb, ""])),
        [(lv_theme_t_ptr, "th"), (lv_obj_t_ptr, "obj")],
        capture="",
    )

    return [
        "auto current_theme_p = lv_disp_get_theme(NULL)",
        f"*{tvar} = *current_theme_p",
        f"lv_theme_set_parent({tvar}, current_theme_p)",
        f"lv_theme_set_apply_cb({tvar}, {lamb})",
        f"lv_disp_set_theme(NULL, {tvar})",
    ]


lv_uses = {
    "USER_DATA",
    "LOG",
}
lv_temp_vars = set()  # Temporary variables
lv_groups = set()  # Widget group names
lv_defines = {}  # Dict of #defines to provide as build flags


def add_temp_var(var_type, var_name):
    if var_name in lv_temp_vars:
        return []
    lv_temp_vars.add(var_name)
    return [f"{var_type} * {var_name}"]


def add_group(name):
    fullname = f"lv_esp_group_{name}"
    if name not in lv_groups:
        cgen(f"static lv_group_t * {fullname} = lv_group_create()")
        lv_groups.add(name)
    return fullname


def add_define(macro, value="1"):
    if macro in lv_defines and lv_defines[macro] != value:
        LOGGER.error(f"Redefinition of {macro} - was {lv_defines[macro]}, now {value}")
    lv_defines[macro] = value


def collect_props(config):
    props = {}
    for prop in [*STYLE_PROPS, CONF_STYLES]:
        if prop in config:
            props[prop] = config[prop]
    return props


def collect_states(config):
    states = {CONF_DEFAULT: collect_props(config)}
    for state in STATES:
        if state in config:
            states[state] = collect_props(config[state])
    return states


def collect_parts(config):
    parts = {CONF_MAIN: collect_states(config)}
    for part in PARTS:
        if part in config:
            parts[part] = collect_states(config[part])
    return parts


def set_obj_properties(var, config):
    """Return a list of C++ statements to apply properties to an lv_obj_t"""
    init = []
    parts = collect_parts(config)
    for part, states in parts.items():
        for state, props in states.items():
            lv_state = f"(int)LV_STATE_{state.upper()}|(int)LV_PART_{part.upper()}"
            for prop, value in props.items():
                if prop == CONF_STYLES:
                    for style_id in value:
                        init.append(f"lv_obj_add_style({var}, {style_id}, {lv_state})")
                else:
                    init.append(f"lv_obj_set_style_{prop}({var}, {value}, {lv_state})")
    if layout := config.get(CONF_LAYOUT):
        layout = layout.upper()
        init.append(f"lv_obj_set_layout({var}, {layout})")
        if layout == "LV_LAYOUT_FLEX":
            lv_uses.add("FLEX")
            init.append(f"lv_obj_set_flex_flow({var}, {config[CONF_FLEX_FLOW]})")
        if layout == "LV_LAYOUT_GRID":
            lv_uses.add("GRID")
    if states := config.get(CONF_STATE):
        adds = set()
        clears = set()
        for key, value in states.items():
            if value:
                adds.add(key)
            else:
                clears.add(key)
        if adds:
            adds = "|".join(map(lambda s: f"(int)LV_STATE_{s.upper()}", adds))
            init.append(f"lv_obj_add_state({var}, {adds})")
        if clears:
            clears = "|".join(map(lambda s: f"(int)LV_STATE_{s.upper()}", clears))
            init.append(f"lv_obj_clear_state({var}, {clears})")
    return init


async def create_lv_obj(t, lv_component, config, parent):
    """Write object creation code for an object extending lv_obj_t"""
    init = []
    var = cg.Pvariable(config[CONF_ID], cg.nullptr, type_=lv_obj_t)
    init.append(f"{var} = lv_{t}_create({parent})")
    if CONF_GROUP in config:
        init.append(f"lv_group_add_obj({add_group(config[CONF_GROUP])}, {var})")
    init.extend(set_obj_properties(var, config))
    if CONF_WIDGETS in config:
        for widg in config[CONF_WIDGETS]:
            (obj, ext_init) = await widget_to_code(lv_component, widg, var)
            init.extend(ext_init)
    return var, init


async def checkbox_to_code(lv_component, var, checkbox):
    """For a text object, create and set text"""
    if CONF_TEXT in checkbox:
        (value, lamb) = await get_text_lambda(checkbox[CONF_TEXT])
        if value is not None:
            return [f'lv_checkbox_set_text({var}, "{value}")']
        if lamb is not None:
            return [f"{lv_component}->add_updater(new Checkbox({var}, {lamb}))"]
    return []


async def label_to_code(lv_component, var, label):
    """For a text object, create and set text"""
    if CONF_TEXT in label:
        (value, lamb) = await get_text_lambda(label[CONF_TEXT])
        if value is not None:
            return [f'lv_label_set_text({var}, "{value}")']
        if lamb is not None:
            return [f"{lv_component}->add_updater(new Label({var}, {lamb}))"]
    return []


async def obj_to_code(_, var, obj):
    return []


async def btn_to_code(_, var, btn):
    return []


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


async def get_boolean_lambda(value):
    lamb = None
    if isinstance(value, core.Lambda):
        lamb = await cg.process_lambda(value, [], return_type=cg.bool_)
        value = None
    elif isinstance(value, core.ID):
        lamb = "[] {" f"return {value}->get_state();" "}"
        value = None
    return value, lamb


async def get_text_lambda(value):
    lamb = None
    if isinstance(value, core.Lambda):
        lamb = await cg.process_lambda(value, [], return_type=cg.const_char_ptr)
        value = None
    elif isinstance(value, core.ID):
        lamb = "[] {" f"return {value}->get_state().c_str();" "}"
        value = None
    return value, lamb


async def get_value_lambda(value):
    if value is None:
        return None, "nullptr"
    if isinstance(value, core.Lambda):
        return None, await cg.process_lambda(value, [], return_type=float)
    elif isinstance(value, core.ID):
        return None, "[] {" f"return {value}->get_state();" "}"
    return value, "nullptr"


async def get_end_value(config):
    return await get_value_lambda(config.get(CONF_END_VALUE))


async def get_start_value(config):
    if CONF_START_VALUE in config:
        value = config[CONF_START_VALUE]
    else:
        value = config.get(CONF_VALUE)
    return await get_value_lambda(value)


meter_indicators = {}


async def meter_to_code(lv_component, var, meter):
    """For a meter object, create and set parameters"""

    global meter_indicators
    init = []
    s = "meter_var"
    init.extend(add_temp_var("lv_meter_scale_t", s))
    if CONF_SCALES in meter:
        for scale in meter[CONF_SCALES]:
            rotation = 90 + (360 - scale[CONF_ANGLE_RANGE]) / 2
            if CONF_ROTATION in scale:
                rotation = scale[CONF_ROTATION]
            init.append(f"{s} = lv_meter_add_scale({var})")
            init.append(
                f"lv_meter_set_scale_range({var}, {s}, {scale[CONF_RANGE_FROM]},"
                + f"{scale[CONF_RANGE_TO]}, {scale[CONF_ANGLE_RANGE]}, {rotation})",
            )
            if CONF_TICKS in scale:
                ticks = scale[CONF_TICKS]
                init.append(
                    f"lv_meter_set_scale_ticks({var}, {s}, {ticks[CONF_COUNT]},"
                    + f"{ticks[CONF_WIDTH]}, {ticks[CONF_LENGTH]}, {ticks[CONF_COLOR]})"
                )
                if CONF_MAJOR in ticks:
                    major = ticks[CONF_MAJOR]
                    init.append(
                        f"lv_meter_set_scale_major_ticks({var}, {s}, {major[CONF_STRIDE]},"
                        + f"{major[CONF_WIDTH]}, {major[CONF_LENGTH]}, {major[CONF_COLOR]},"
                        + f"{major[CONF_LABEL_GAP]})"
                    )
            if CONF_INDICATORS in scale:
                for indicator in scale[CONF_INDICATORS]:
                    (t, v) = next(iter(indicator.items()))
                    ivar = cg.new_variable(
                        v[CONF_ID], cg.nullptr, type_=lv_meter_indicator_t_ptr
                    )
                    # Enable getting the meter to which this belongs.
                    meter_indicators[v[CONF_ID]] = var
                    if t == CONF_LINE:
                        init.append(
                            f"{ivar} = lv_meter_add_needle_line({var}, {s}, {v[CONF_WIDTH]},"
                            + f"{v[CONF_COLOR]}, {v[CONF_R_MOD]})"
                        )
                    if t == CONF_ARC:
                        init.append(
                            f"{ivar} = lv_meter_add_arc({var}, {s}, {v[CONF_WIDTH]},"
                            + f"{v[CONF_COLOR]}, {v[CONF_R_MOD]})"
                        )
                    if t == CONF_TICKS:
                        color_end = v[CONF_COLOR_START]
                        if CONF_COLOR_END in v:
                            color_end = v[CONF_COLOR_END]
                        init.append(
                            f"{ivar} = lv_meter_add_scale_lines({var}, {s}, {v[CONF_COLOR_START]},"
                            + f"{color_end}, {v[CONF_LOCAL]}, {v[CONF_R_MOD]})"
                        )
                    (start_value, start_lamb) = await get_start_value(v)
                    (end_value, end_lamb) = await get_end_value(v)
                    if start_value is not None:
                        init.append(
                            f"lv_meter_set_indicator_start_value({var}, {ivar}, {start_value})"
                        )
                    if end_value is not None:
                        init.append(
                            f"lv_meter_set_indicator_end_value({var}, {ivar}, {end_value})"
                        )
                    if start_lamb != "nullptr" or end_lamb != "nullptr":
                        init.append(
                            f"{lv_component}->add_updater(new Indicator({var}, {ivar}, {start_lamb}, {end_lamb}))"
                        )

    return init


async def arc_to_code(lv_component, var, arc):
    init = [
        f"lv_arc_set_range({var}, {arc[CONF_MIN_VALUE]}, {arc[CONF_MAX_VALUE]})",
        f"lv_arc_set_bg_angles({var}, {arc[CONF_START_ANGLE]}, {arc[CONF_END_ANGLE]})",
        f"lv_arc_set_rotation({var}, {arc[CONF_ROTATION]})",
        f"lv_arc_set_mode({var}, {arc[CONF_MODE]})",
        f"lv_arc_set_change_rate({var}, {arc[CONF_CHANGE_RATE]})",
    ]
    (value, lamb) = await get_start_value(arc)
    if value is not None:
        init.append(f"lv_arc_set_value({var}, {value})")
    if lamb != "nullptr":
        init.append(f"{lv_component}->add_updater(new Arc({var}, {lamb})")
    return init


async def slider_to_code(lv_component, var, slider):
    init = [
        f"lv_slider_set_range({var}, {slider[CONF_MIN_VALUE]}, {slider[CONF_MAX_VALUE]})",
        f"lv_slider_set_mode({var}, {slider[CONF_MODE]})",
    ]
    (value, lamb) = await get_start_value(slider)
    if value is not None:
        init.append(f"lv_slider_set_value({var}, {value}, LV_ANIM_OFF)")
    if lamb != "nullptr":
        init.append(
            f"{lv_component}->add_updater(new Slider({var}, {lamb}, {slider[CONF_ANIMATED]}))"
        )
    return init


async def bar_to_code(lv_component, var, bar):
    init = [
        f"lv_bar_set_range({var}, {bar[CONF_MIN_VALUE]}, {bar[CONF_MAX_VALUE]})",
        f"lv_bar_set_mode({var}, {bar[CONF_MODE]})",
    ]
    (value, lamb) = await get_start_value(bar)
    if value is not None:
        init.append(f"lv_bar_set_value({var}, {value}, LV_ANIM_OFF)")
    if lamb != "nullptr":
        init.append(
            f"{lv_component}->add_updater(new Bar({var}, {lamb}, {bar[CONF_ANIMATED]}))"
        )
    return init


async def widget_to_code(lv_component, widget, parent):
    (t, v) = next(iter(widget.items()))
    lv_uses.add(t)
    (var, init) = await create_lv_obj(t, lv_component, v, parent)
    fun = f"{t}_to_code"
    if fun in globals():
        fun = globals()[fun]
        init.extend(await fun(lv_component, var, v))
    else:
        raise cv.Invalid(f"No handler for widget `{t}'")
    return var, init


async def rotary_encoders_to_code(var, config):
    init = []
    if CONF_ROTARY_ENCODERS not in config:
        return init
    for index, encoder in enumerate(config[CONF_ROTARY_ENCODERS]):
        sensor = await cg.get_variable(encoder[CONF_SENSOR])
        cgen(f"static bool encoder_pressed_{index}{{}}")
        cgen(f"static uint32_t encoder_count_{index}, encoder_last_{index}")
        cgen(f"static lv_indev_drv_t encoder_drv_{index};")
        cgen(f"lv_indev_drv_init(&encoder_drv_{index})")
        cgen(f"encoder_drv_{index}.type = LV_INDEV_TYPE_ENCODER")
        cgen(
            f"encoder_drv_{index}.read_cb =",
            "[](lv_indev_drv_t *drv, lv_indev_data_t *data) {",
            f"  data->state = encoder_pressed_{index} ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;",
            f"  data->enc_diff = encoder_count_{index} - encoder_last_{index};",
            f"  encoder_last_{index} = encoder_count_{index}; }}",
        )
        init.extend(add_temp_var("lv_indev_t", "lv_indev_temp_"))
        init.extend(
            [
                f"lv_indev_temp_ = lv_indev_drv_register(&encoder_drv_{index})",
                f"{sensor}->register_listener([](uint32_t count) {{ encoder_count_{index} = count;}})",
            ]
        )
        if CONF_GROUP in encoder:
            group = add_group(encoder[CONF_GROUP])
            init.append(f"lv_indev_set_group(lv_indev_temp_, {group})")
        if CONF_BINARY_SENSOR in encoder:
            binary_sensor = await cg.get_variable(encoder[CONF_BINARY_SENSOR])
            init.extend(
                [
                    f"{binary_sensor}->add_on_state_callback([](bool state) {{"
                    + "encoder_pressed_{index} = state && {var}->is_paused();}})"
                ]
            )
        return init


async def touchscreens_to_code(var, config):
    init = []
    if CONF_TOUCHSCREENS not in config:
        return init
    lv_uses.add("TOUCHSCREEN")
    for touchconf in config[CONF_TOUCHSCREENS]:
        touchscreen = await cg.get_variable(touchconf[CONF_TOUCHSCREEN_ID])
        listener = cg.new_Pvariable(touchconf[CONF_ID])
        await cg.register_parented(listener, var)
        init.extend(
            [
                f"lv_indev_drv_register(&{listener}->drv)",
                f"{touchscreen}->register_listener({listener})",
            ]
        )
    return init


async def to_code(config):
    cg.add_library("lvgl/lvgl", "8.3.9")
    for comp in lvgl_components_required:
        add_define(f"LVGL_USES_{comp.upper()}")
    add_define("_STRINGIFY(x)", "_STRINGIFY_(x)")
    add_define("_STRINGIFY_(x)", "#x")
    add_define("LV_CONF_SKIP", "1")
    add_define("LV_TICK_CUSTOM", "1")
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
    cgen("lv_init()")
    if CONF_ROTARY_ENCODERS in config:  # or CONF_KEYBOARDS in config
        cgen("lv_group_set_default(lv_group_create())")
    init = []
    if style_defs := config.get(CONF_STYLE_DEFINITIONS, []):
        styles_to_code(style_defs)
    if theme := config[CONF_THEME]:
        init.extend(await theme_to_code(theme))
    for widg in config[CONF_WIDGETS]:
        (obj, ext_init) = await widget_to_code(lv_component, widg, "lv_scr_act()")
        init.extend(ext_init)

    init.extend(await touchscreens_to_code(lv_component, config))
    init.extend(await rotary_encoders_to_code(lv_component, config))
    init.extend(set_obj_properties("lv_scr_act()", config))
    if on_idle := config.get(CONF_ON_IDLE):
        for conf in on_idle:
            trigger = cg.new_Pvariable(
                conf[CONF_TRIGGER_ID],
                lv_component,
                conf[CONF_TIMEOUT].total_milliseconds,
            )
            await automation.build_automation(trigger, [], conf)

    await add_init_lambda(lv_component, init)
    for use in lv_uses:
        core.CORE.add_build_flag(f"-DLV_USE_{use.upper()}=1")
    for macro, value in lv_defines.items():
        cg.add_build_flag(f"-D\\'{macro}\\'=\\'{value}\\'")


def modify_schema(widget_type):
    lv_type = globals()[f"lv_{widget_type}_t"]
    schema = part_schema(widget_type).extend(
        {
            cv.Required(CONF_ID): cv.use_id(lv_type),
            cv.Optional(CONF_STATE): SET_STATE_SCHEMA,
        }
    )
    if extras := globals().get(f"{widget_type.upper()}_SCHEMA"):
        return schema.extend(extras)
    return schema


async def update_to_code(config, action_id, obj, init, template_arg):
    if config is not None:
        init.extend(set_obj_properties(obj, config))
    lamb = await cg.process_lambda(core.Lambda(";\n".join([*init, ""])), [])
    var = cg.new_Pvariable(action_id, template_arg, lamb)
    return var


@automation.register_action("lvgl.obj.update", ObjUpdateAction, modify_schema(CONF_OBJ))
async def obj_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    return await update_to_code(config, action_id, obj, [], template_arg)


@automation.register_action(
    "lvgl.checkbox.update",
    ObjUpdateAction,
    modify_schema(CONF_CHECKBOX),
)
async def checkbox_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = []
    if CONF_TEXT in config:
        (value, lamb) = await get_text_lambda(config[CONF_TEXT])
        if value is not None:
            init.append(f'lv_checkbox_set_text({obj}, "{value}")')
        if lamb is not None:
            init.append(f"lv_checkbox_set_text({obj}, {lamb}())")
    return await update_to_code(config, action_id, obj, init, template_arg)


@automation.register_action(
    "lvgl.label.update",
    ObjUpdateAction,
    modify_schema(CONF_LABEL),
)
async def label_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = []
    if CONF_TEXT in config:
        (value, lamb) = await get_text_lambda(config[CONF_TEXT])
        if value is not None:
            init.append(f'lv_label_set_text({obj}, "{value}")')
        if lamb is not None:
            init.append(f"lv_label_set_text({obj}, {lamb}())")
    return await update_to_code(config, action_id, obj, init, template_arg)


def indicator_update_schema(base):
    return base.extend({cv.Required(CONF_ID): cv.use_id(lv_meter_indicator_t)})


@automation.register_action(
    "lvgl.indicator.line.update",
    ObjUpdateAction,
    indicator_update_schema(INDICATOR_LINE_SCHEMA),
)
async def indicator_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    meter = meter_indicators[config[CONF_ID]]
    init = []
    (start_value, start_lamb) = await get_start_value(config)
    (end_value, end_lamb) = await get_end_value(config)
    selector = "start_" if end_value is not None or end_lamb != "nullptr" else ""
    if start_value is not None:
        init.append(
            f"lv_meter_set_indicator_{selector}value({meter},{obj}, {start_value})"
        )
    elif start_lamb != "nullptr":
        init.append(
            f"lv_meter_set_indicator_{selector}value({meter},{obj}, {start_lamb}())"
        )
    if end_value is not None:
        init.append(f"lv_meter_set_indicator_end_value({meter},{obj}, {end_value})")
    elif end_lamb != "nullptr":
        init.append(f"lv_meter_set_indicator_end_value({meter},{obj}, {end_lamb}())")
    return await update_to_code(None, action_id, obj, init, template_arg)


@automation.register_action(
    "lvgl.arc.update",
    ObjUpdateAction,
    modify_schema(CONF_ARC),
)
async def arc_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = []
    animated = config[CONF_ANIMATED]
    (value, lamb) = await get_value_lambda(config.get(CONF_VALUE))
    print(value, lamb)
    if value is not None:
        init.append(f"lv_arc_set_value({obj}, {value}, {animated})")
    elif lamb != "nullptr":
        init.append(f"lv_arc_set_value({obj}, {lamb}(), {animated})")
    return await update_to_code(config, action_id, obj, init, template_arg)


@automation.register_action(
    "lvgl.slider.update",
    ObjUpdateAction,
    modify_schema(CONF_SLIDER),
)
async def slider_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = []
    animated = config[CONF_ANIMATED]
    (value, lamb) = await get_value_lambda(config.get(CONF_VALUE))
    if value is not None:
        init.append(f"lv_slider_set_value({obj}, {value}, {animated})")
    elif lamb != "nullptr":
        init.append(f"lv_slider_set_value({obj}, {lamb}(), {animated})")
    return await update_to_code(config, action_id, obj, init, template_arg)


@automation.register_action(
    "lvgl.img.update",
    ObjUpdateAction,
    modify_schema(CONF_IMG),
)
async def img_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = []
    if CONF_SRC in config:
        init.append(f"lv_img_set_src({obj}, lv_img_from({config[CONF_SRC]}))")
    return await update_to_code(config, action_id, obj, init, template_arg)


@automation.register_action(
    "lvgl.pause",
    PauseAction,
    {
        cv.GenerateID(): cv.use_id(LvglComponent),
    },
)
async def pause_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_paused(True))
    return var


@automation.register_action(
    "lvgl.resume",
    PauseAction,
    {
        cv.GenerateID(): cv.use_id(LvglComponent),
    },
)
async def resume_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_paused(False))
    return var


@automation.register_condition(
    "lvgl.is_idle",
    LvglCondition,
    LVGL_SCHEMA.extend(
        {
            cv.Required(CONF_TIMEOUT): cv.positive_time_period_seconds,
        }
    ),
)
async def lvgl_is_idle(config, condition_id, template_arg, args):
    var = cg.new_Pvariable(condition_id, template_arg)
    lvgl = config[CONF_LVGL_ID]
    timeout = config[CONF_TIMEOUT].total_milliseconds
    await cg.register_parented(var, lvgl)
    cg.add(
        var.set_condition_lambda(
            cg.RawExpression(f"[] {{ return {lvgl}->is_idle({timeout}); }}")
        )
    )
    return var


@automation.register_condition(
    "lvgl.is_paused",
    LvglCondition,
    LVGL_SCHEMA,
)
async def lvgl_is_paused(config, condition_id, template_arg, args):
    var = cg.new_Pvariable(condition_id, template_arg)
    lvgl = config[CONF_LVGL_ID]
    await cg.register_parented(var, lvgl)
    cg.add(
        var.set_condition_lambda(
            cg.RawExpression(f"[] {{ return {lvgl}->is_paused(); }}")
        )
    )
    return var


ReturnNotifyTrigger = lvgl_ns.class_(
    "ReturnNotifyTrigger", automation.Trigger.template()
)


@automation.register_action(
    "lvgl.notification",
    lvgl_ns.class_("NotifyAction", automation.Action),
    cv.Schema(
        {
            cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
            cv.Optional(CONF_TITLE, default=""): cv.templatable(cv.string),
            cv.Required(CONF_MESSAGE): cv.templatable(cv.string),
            cv.Optional(CONF_CLOSE_BUTTON, default=False): cv.boolean,
            cv.Optional(CONF_BUTTONS): cv.All([cv.string]),
            cv.Optional(CONF_ON_RETURN): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ReturnNotifyTrigger),
                }
            ),
        },
    ),
)
async def lvgl_notify_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_LVGL_ID])
    if CONF_BUTTONS not in config:
        config[CONF_BUTTONS] = []
    cg.add(
        var.set_notification(
            config[CONF_TITLE],
            config[CONF_MESSAGE],
            config[CONF_CLOSE_BUTTON],
            config[CONF_BUTTONS],
        )
    )
    for conf in config.get(CONF_ON_RETURN, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
    return var
