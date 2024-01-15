import logging
from esphome.core import (
    CORE,
    ID,
    Lambda,
)
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components.image import Image_
from esphome.coroutine import FakeAwaitable
from esphome.components.touchscreen import (
    Touchscreen,
    CONF_TOUCHSCREEN_ID,
)
from esphome.schema_extractors import SCHEMA_EXTRACT
from esphome.components.display import Display
from esphome.components.rotary_encoder.sensor import RotaryEncoderSensor
from esphome.components.binary_sensor import BinarySensor
from esphome.const import (
    CONF_BINARY_SENSOR,
    CONF_BRIGHTNESS,
    CONF_BUFFER_SIZE,
    CONF_COLOR,
    CONF_COUNT,
    CONF_GROUP,
    CONF_ID,
    CONF_LED,
    CONF_LENGTH,
    CONF_LOCAL,
    CONF_MIN_VALUE,
    CONF_MAX_VALUE,
    CONF_MODE,
    CONF_OPTIONS,
    CONF_PAGES,
    CONF_RANGE_FROM,
    CONF_RANGE_TO,
    CONF_ROTATION,
    CONF_SENSOR,
    CONF_STATE,
    CONF_TIME,
    CONF_TIMEOUT,
    CONF_TRIGGER_ID,
    CONF_VALUE,
    CONF_WIDTH,
)
from esphome.cpp_generator import (
    LambdaExpression,
)
from .defines import (
    # widgets
    CONF_ARC,
    CONF_BAR,
    CONF_BTN,
    CONF_BTNMATRIX,
    CONF_CANVAS,
    CONF_CHECKBOX,
    CONF_DROPDOWN,
    CONF_DROPDOWN_LIST,
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
    BTNMATRIX_CTRLS,
    # Input devices
    CONF_ROTARY_ENCODERS,
    CONF_TOUCHSCREENS,
    LV_SYMBOLS,
    DIRECTIONS,
    ROLLER_MODES,
    CONF_PAGE,
    LV_ANIM,
    LV_EVENT_TRIGGERS,
    LV_LONG_MODES,
    LV_EVENT,
)

from .lv_validation import (
    lv_one_of,
    lv_opacity,
    lv_bool,
    lv_stop_value,
    lv_any_of,
    lv_size,
    lv_font,
    lv_angle,
    pixels_or_percent,
    lv_zoom,
    lv_animated,
    join_enums,
    lv_fonts_used,
    lv_uses,
    lv_color,
    REQUIRED_COMPONENTS,
    lvgl_components_required,
    cv_int_list,
    lv_value,
    lv_text_value,
    validate_max_min,
    lv_option_string,
    lv_id_name,
    requires_component,
)

# import auto
DOMAIN = "lvgl"
DEPENDENCIES = ["display"]
CODEOWNERS = ["@clydebarrow"]
LOGGER = logging.getLogger(__name__)

char_ptr_const = cg.global_ns.namespace("char").operator("ptr")
lv_event_code_t = cg.global_ns.enum("lv_event_code_t")
lvgl_ns = cg.esphome_ns.namespace("lvgl")
LvglComponent = lvgl_ns.class_("LvglComponent", cg.PollingComponent)
LvglComponentPtr = LvglComponent.operator("ptr")
LVTouchListener = lvgl_ns.class_("LVTouchListener")
LVRotaryEncoderListener = lvgl_ns.class_("LVRotaryEncoderListener")
IdleTrigger = lvgl_ns.class_("IdleTrigger", automation.Trigger.template())
FontEngine = lvgl_ns.class_("FontEngine")
ObjUpdateAction = lvgl_ns.class_("ObjUpdateAction", automation.Action)
LvglCondition = lvgl_ns.class_("LvglCondition", automation.Condition)
LvglAction = lvgl_ns.class_("LvglAction", automation.Action)
lv_lambda_t = lvgl_ns.class_("LvLambdaType")
# lv_lambda_ptr_t = lvgl_ns.class_("LvLambdaType").operator("ptr")

# Can't use the native type names here, since ESPHome munges variable names and they conflict
lv_pseudo_button_t = lvgl_ns.class_("LvPseudoButton")
LvBtnmBtn = lvgl_ns.class_("LvBtnmBtn", lv_pseudo_button_t)
lv_obj_t = cg.global_ns.class_("LvObjType", lv_pseudo_button_t)
lv_page_t = cg.global_ns.class_("LvPageType")
lv_screen_t = cg.global_ns.class_("LvScreenType")
lv_point_t = cg.global_ns.struct("LvPointType")
lv_obj_t_ptr = lv_obj_t.operator("ptr")
lv_style_t = cg.global_ns.struct("LvStyleType")
lv_theme_t = cg.global_ns.struct("LvThemeType")
lv_theme_t_ptr = lv_theme_t.operator("ptr")
lv_meter_indicator_t = cg.global_ns.struct("LvMeterIndicatorType")
lv_indicator_t = cg.global_ns.struct("LvMeterIndicatorType")
lv_meter_indicator_t_ptr = lv_meter_indicator_t.operator("ptr")
lv_label_t = cg.MockObjClass("LvLabelType", parents=[lv_obj_t])
lv_dropdown_list_t = cg.MockObjClass("LvDropdownListType", parents=[lv_obj_t])
lv_meter_t = cg.MockObjClass("LvMeterType", parents=[lv_obj_t])
lv_slider_t = cg.MockObjClass("LvSliderType", parents=[lv_obj_t])
lv_btn_t = cg.MockObjClass("LvBtnType", parents=[lv_obj_t])
lv_checkbox_t = cg.MockObjClass("LvCheckboxType", parents=[lv_obj_t])
lv_line_t = cg.MockObjClass("LvLineType", parents=[lv_obj_t])
lv_img_t = cg.MockObjClass("LvImgType", parents=[lv_obj_t])
lv_arc_t = cg.MockObjClass("LvArcType", parents=[lv_obj_t])
lv_bar_t = cg.MockObjClass("LvBarType", parents=[lv_obj_t])
lv_disp_t_ptr = cg.global_ns.struct("lv_disp_t").operator("ptr")
lv_btnmatrix_t = cg.MockObjClass("LvBtnmatrixType", parents=[lv_obj_t])
lv_canvas_t = cg.MockObjClass("LvCanvasType", parents=[lv_obj_t])
lv_dropdown_t = cg.MockObjClass("LvDropdownType", parents=[lv_obj_t])
lv_roller_t = cg.MockObjClass("LvRollerType", parents=[lv_obj_t])
lv_led_t = cg.MockObjClass("LvLedType", parents=[lv_obj_t])
lv_switch_t = cg.MockObjClass("LvSwitchType", parents=[lv_obj_t])
lv_table_t = cg.MockObjClass("LvTableType", parents=[lv_obj_t])
lv_textarea_t = cg.MockObjClass("LvTextareaType", parents=[lv_obj_t])

CONF_ACTION = "action"
CONF_ADJUSTABLE = "adjustable"
CONF_ANGLE_RANGE = "angle_range"
CONF_ANIMATED = "animated"
CONF_ANIMATION = "animation"
CONF_BACKGROUND_STYLE = "background_style"
CONF_BUTTONS = "buttons"
CONF_BYTE_ORDER = "byte_order"
CONF_CHANGE_RATE = "change_rate"
CONF_COLOR_DEPTH = "color_depth"
CONF_COLOR_END = "color_end"
CONF_COLOR_START = "color_start"
CONF_CONTROL = "control"
CONF_CRITICAL_VALUE = "critical_value"
CONF_DEFAULT = "default"
CONF_DIR = "dir"
CONF_DISPLAY_ID = "display_id"
CONF_DISPLAYS = "displays"
CONF_END_ANGLE = "end_angle"
CONF_END_VALUE = "end_value"
CONF_FLAGS = "flags"
CONF_FLEX_FLOW = "flex_flow"
CONF_HOME = "home"
CONF_INDICATORS = "indicators"
CONF_LABEL_GAP = "label_gap"
CONF_LAYOUT = "layout"
CONF_LINE_WIDTH = "line_width"
CONF_LOG_LEVEL = "log_level"
CONF_LONG_PRESS_TIME = "long_press_time"
CONF_LONG_PRESS_REPEAT_TIME = "long_press_repeat_time"
CONF_LVGL_COMPONENT = "lvgl_component"
CONF_LVGL_ID = "lvgl_id"
CONF_LONG_MODE = "long_mode"
CONF_MAJOR = "major"
CONF_OBJ = "obj"
CONF_ON_IDLE = "on_idle"
CONF_ONE_CHECKED = "one_checked"
CONF_NEXT = "next"
CONF_PIVOT_X = "pivot_x"
CONF_PIVOT_Y = "pivot_y"
CONF_POINTS = "points"
CONF_PREVIOUS = "previous"
CONF_ROWS = "rows"
CONF_R_MOD = "r_mod"
CONF_RECOLOR = "recolor"
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
CONF_SYMBOL = "symbol"
CONF_SELECTED_INDEX = "selected_index"
CONF_SKIP = "skip"
CONF_TEXT = "text"
CONF_TOP_LAYER = "top_layer"
CONF_THEME = "theme"
CONF_WIDGET = "widget"
CONF_WIDGETS = "widgets"

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
    CONF_LED: (CONF_MAIN,),
    CONF_LINE: (CONF_MAIN,),
    CONF_DROPDOWN_LIST: (CONF_MAIN, CONF_SCROLLBAR, CONF_SELECTED),
    CONF_METER: (CONF_MAIN,),
    CONF_OBJ: (CONF_MAIN,),
    CONF_PAGE: (CONF_MAIN,),
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

# List the LVGL built-in fonts that are available

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

# Map of widgets to their config, used for trigger generation
widget_list = []


def modify_schema(widget_type):
    lv_type = globals()[f"lv_{widget_type}_t"]
    schema = (
        part_schema(widget_type)
        .extend(
            {
                cv.Required(CONF_ID): cv.use_id(lv_type),
                cv.Optional(CONF_STATE): SET_STATE_SCHEMA,
            }
        )
        .extend(FLAG_SCHEMA)
    )
    if extras := globals().get(f"{widget_type.upper()}_MODIFY_SCHEMA"):
        return schema.extend(extras)
    if extras := globals().get(f"{widget_type.upper()}_SCHEMA"):
        return schema.extend(extras)
    return schema


def generate_id(base):
    generate_id.counter += 1
    return f"lvgl_{base}_{generate_id.counter}"


generate_id.counter = 0


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


def part_schema(parts):
    if isinstance(parts, str):
        parts = WIDGET_TYPES[parts]
    return cv.Schema({cv.Optional(part): STATE_SCHEMA for part in parts}).extend(
        STATE_SCHEMA
    )


AUTOMATION_SCHEMA = {
    cv.Optional(event): automation.validate_automation(
        {
            cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                automation.Trigger.template()
            ),
        }
    )
    for event in LV_EVENT_TRIGGERS
}

TEXT_SCHEMA = cv.Schema(
    {
        cv.Exclusive(CONF_TEXT, CONF_TEXT): lv_text_value,
        cv.Exclusive(CONF_SYMBOL, CONF_TEXT): lv_one_of(LV_SYMBOLS, "LV_SYMBOL_"),
    }
)

STYLE_SCHEMA = cv.Schema({cv.Optional(k): v for k, v in STYLE_PROPS.items()}).extend(
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

LINE_SCHEMA = {cv.Optional(CONF_POINTS): cv_point_list}

IMG_SCHEMA = {cv.Required(CONF_SRC): cv.use_id(Image_)}

# Schema for a single button in a btnmatrix
BTNM_BTN_SCHEMA = cv.Schema(
    {
        cv.Exclusive(CONF_TEXT, CONF_TEXT): cv.string,
        cv.Exclusive(CONF_SYMBOL, CONF_TEXT): lv_one_of(LV_SYMBOLS, "LV_SYMBOL_"),
        cv.GenerateID(): cv.declare_id(LvBtnmBtn),
        cv.Optional(CONF_WIDTH, default=1): cv.positive_int,
        cv.Optional(CONF_CONTROL): cv.ensure_list(
            cv.Schema({cv.Optional(k.lower()): cv.boolean for k in BTNMATRIX_CTRLS})
        ),
    }
).extend(AUTOMATION_SCHEMA)

BTNMATRIX_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ONE_CHECKED, default=False): cv.boolean,
        cv.Required(CONF_ROWS): cv.ensure_list(
            cv.Schema(
                {
                    cv.Required(CONF_BUTTONS): cv.ensure_list(BTNM_BTN_SCHEMA),
                }
            )
        ),
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

METER_SCHEMA = {cv.Optional(CONF_SCALES): cv.ensure_list(SCALE_SCHEMA)}

PAGE_SCHEMA = {
    cv.Optional(CONF_SKIP, default=False): lv_bool,
}

LABEL_SCHEMA = TEXT_SCHEMA.extend(
    {
        cv.Optional(CONF_RECOLOR): lv_bool,
        cv.Optional(CONF_LONG_MODE): lv_one_of(LV_LONG_MODES, prefix="LV_LABEL_LONG_"),
    }
)

CHECKBOX_SCHEMA = TEXT_SCHEMA

DROPDOWN_BASE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_SYMBOL): lv_one_of(LV_SYMBOLS, "LV_SYMBOL_"),
        cv.Optional(CONF_SELECTED_INDEX): cv.templatable(cv.int_),
        cv.Optional(CONF_DIR, default="BOTTOM"): lv_one_of(DIRECTIONS, "LV_DIR_"),
        cv.Optional(CONF_DROPDOWN_LIST): part_schema(CONF_DROPDOWN_LIST),
    }
)

DROPDOWN_SCHEMA = DROPDOWN_BASE_SCHEMA.extend(
    {
        cv.Required(CONF_OPTIONS): cv.ensure_list(lv_option_string),
    }
)

DROPDOWN_MODIFY_SCHEMA = DROPDOWN_BASE_SCHEMA

ROLLER_BASE_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_SELECTED_INDEX): cv.templatable(cv.int_),
        cv.Optional(CONF_MODE, default="NORMAL"): lv_one_of(
            ROLLER_MODES, "LV_ROLLER_MODE_"
        ),
    }
)

ROLLER_SCHEMA = ROLLER_BASE_SCHEMA.extend(
    {
        cv.Required(CONF_OPTIONS): cv.ensure_list(lv_option_string),
    }
)

ROLLER_MODIFY_SCHEMA = ROLLER_BASE_SCHEMA.extend(
    {
        cv.Optional(CONF_ANIMATED, default=True): lv_animated,
    }
)

LED_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_COLOR): lv_color,
        cv.Optional(CONF_BRIGHTNESS): cv.templatable(cv.percentage),
    }
)

# For use by platform components
LVGL_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
    }
)


def obj_schema(parts=(CONF_MAIN,)):
    return (
        part_schema(parts)
        .extend(FLAG_SCHEMA)
        .extend(AUTOMATION_SCHEMA)
        .extend(
            cv.Schema(
                {
                    cv.Optional(CONF_LAYOUT): lv_one_of(["FLEX", "GRID"], "LV_LAYOUT_"),
                    cv.Optional(CONF_FLEX_FLOW, default="ROW_WRAP"): lv_one_of(
                        FLEX_FLOWS, prefix="LV_FLEX_FLOW_"
                    ),
                    cv.Optional(CONF_STATE): SET_STATE_SCHEMA,
                    cv.Optional(CONF_GROUP): lv_id_name,
                }
            )
        )
    )


def container_schema(widget_type):
    lv_type = globals()[f"lv_{widget_type}_t"]
    schema = obj_schema(widget_type)
    if extras := globals().get(f"{widget_type.upper()}_SCHEMA"):
        schema = schema.extend(extras).add_extra(validate_max_min)
    schema = schema.extend({cv.GenerateID(): cv.declare_id(lv_type)})

    # Delayed evaluation for recursion
    def validator(value):
        widgets = cv.Schema(
            {
                cv.Optional(CONF_WIDGETS): cv.ensure_list(WIDGET_SCHEMA),
            }
        )
        result = schema.extend(widgets)
        if value == SCHEMA_EXTRACT:
            return result
        return result(value)

    return validator


def widget_schema(name):
    validator = container_schema(name)
    if required := REQUIRED_COMPONENTS.get(name):
        validator = cv.All(validator, requires_component(required))
    return cv.Exclusive(name, CONF_WIDGETS), validator


WIDGET_SCHEMA = cv.Any(dict(map(widget_schema, WIDGET_TYPES)))


async def get_boolean_value(value):
    if value is None:
        return None
    if isinstance(value, Lambda):
        lamb = await cg.process_lambda(value, [], return_type=cg.bool_)
    elif isinstance(value, ID):
        lamb = f"[] {{ return {value}->get_state(); }}"
    else:
        return value
    return f"{lamb}()"


async def get_text_value(config):
    if (value := config.get(CONF_TEXT)) is not None:
        if isinstance(value, Lambda):
            return (
                f"{await cg.process_lambda(value, [], return_type=cg.const_char_ptr)}()"
            )
        if isinstance(value, ID):
            return "[] {" f"return {value}->get_state().c_str();}}()"
        return cg.safe_exp(value)
    if value := config.get(CONF_SYMBOL):
        return value
    return None


async def get_value_expr(input, return_type=cg.float_):
    if input is None:
        return None
    if isinstance(input, Lambda):
        return f"{await cg.process_lambda(input, [], return_type=return_type)}()"
    if isinstance(input, ID):
        return "[] {" f"return ({return_type}){input}->get_state();" "}"
    return f"(({return_type}){input})"


async def get_end_value(config):
    return await get_value_expr(config.get(CONF_END_VALUE))


async def get_start_value(config):
    if CONF_START_VALUE in config:
        value = config[CONF_START_VALUE]
    else:
        value = config.get(CONF_VALUE)
    return await get_value_expr(value)


async def add_init_lambda(lv_component, init):
    lamb = await cg.process_lambda(
        Lambda(";\n".join([*init, ""])), [(lv_disp_t_ptr, "lv_disp")]
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


theme_widget_map = {}


async def theme_to_code(theme):
    init = []
    for widget, style in theme.items():
        if not isinstance(style, dict):
            continue

        init.extend(set_obj_properties("obj", style))
        lamb = await cg.process_lambda(
            Lambda(";\n".join([*init, ""])),
            [(lv_obj_t_ptr, "obj")],
            capture="",
        )
        apply = f"lv_theme_apply_{widget}"
        theme_widget_map[widget] = apply
        lamb_id = ID(apply, type=lv_lambda_t, is_declaration=True)
        cg.variable(lamb_id, lamb)


lv_temp_vars = set()  # Temporary variables
lv_groups = set()  # Widget group names
lv_defines = {}  # Dict of #defines to provide as build flags


def add_temp_var(var_type, var_name):
    if var_name in lv_temp_vars:
        return []
    lv_temp_vars.add(var_name)
    return [f"{var_type} * {var_name}"]


def add_group(name):
    if name is None:
        return None
    fullname = f"lv_esp_group_{name}"
    if name not in lv_groups:
        cgen(f"static lv_group_t * {fullname} = lv_group_create()")
        lv_groups.add(name)
    return fullname


def add_define(macro, value="1"):
    if macro in lv_defines and lv_defines[macro] != value:
        LOGGER.error(
            "Redefinition of %s - was %s now %s", macro, lv_defines[macro], value
        )
    lv_defines[macro] = value


def collect_props(config):
    props = {}
    for prop in [*STYLE_PROPS, *OBJ_FLAGS, CONF_STYLES, CONF_GROUP]:
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
            if styles := props.get(CONF_STYLES):
                for style_id in styles:
                    init.append(f"lv_obj_add_style({var}, {style_id}, {lv_state})")
            for prop, value in {
                k: v for k, v in props.items() if k in STYLE_PROPS
            }.items():
                init.append(f"lv_obj_set_style_{prop}({var}, {value}, {lv_state})")
    if group := add_group(config.get(CONF_GROUP)):
        init.append(f"lv_group_add_obj({group}, {var})")
    flag_clr = set()
    flag_set = set()
    props = parts[CONF_MAIN][CONF_DEFAULT]
    for prop, value in {k: v for k, v in props.items() if k in OBJ_FLAGS}.items():
        if value:
            flag_set.add(prop)
        else:
            flag_clr.add(prop)
    if flag_set:
        adds = join_enums(flag_set, "LV_OBJ_FLAG_")
        init.append(f"lv_obj_add_flag({var}, {adds})")
    if flag_clr:
        clrs = join_enums(flag_clr, "LV_OBJ_FLAG_")
        init.append(f"lv_obj_clear_flag({var}, {clrs})")

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
            adds = join_enums(adds, "LV_STATE_")
            init.append(f"lv_obj_add_state({var}, {adds})")
        if clears:
            clears = join_enums(clears, "LV_STATE_")
            init.append(f"lv_obj_clear_state({var}, {clears})")
    return init


async def create_lv_obj(t, config, parent):
    """Write object creation code for an object extending lv_obj_t"""
    init = []
    var = cg.Pvariable(config[CONF_ID], cg.nullptr, type_=lv_obj_t)
    init.append(f"{var} = lv_{t}_create({parent})")
    if theme := theme_widget_map.get(t):
        init.append(f"{theme}({var})")
    init.extend(set_obj_properties(var, config))
    # Workaround because theme does not correctly set group
    if CONF_WIDGETS in config:
        for widg in config[CONF_WIDGETS]:
            (_, ext_init) = await widget_to_code(widg, var)
            init.extend(ext_init)
    return var, init


async def checkbox_to_code(var, checkbox_conf):
    """For a text object, create and set text"""
    if (value := await get_text_value(checkbox_conf)) is not None:
        return [f"lv_checkbox_set_text({var}, {value})"]
    return []


async def label_to_code(var, label_conf):
    """For a text object, create and set text"""
    init = []
    if (value := await get_text_value(label_conf)) is not None:
        init.append(f"lv_label_set_text({var}, {value})")
    if mode := label_conf.get(CONF_LONG_MODE):
        init.append(f"lv_label_set_long_mode({var}, {mode})")
    if (recolor := label_conf.get(CONF_RECOLOR)) is not None:
        init.append(f"lv_label_set_recolor({var}, {recolor})")
    return init


async def obj_to_code(var, obj):
    return []


async def page_to_code(config, pconf, index):
    """Write object creation code for an object extending lv_obj_t"""
    init = []
    var = cg.new_Pvariable(pconf[CONF_ID])
    page = f"{var}->page"
    init.append(f"{var}->index = {index}")
    init.append(f"{page} = lv_obj_create(nullptr)")
    if theme := theme_widget_map.get("page"):
        init.append(f"{theme}({var})")
    skip = pconf[CONF_SKIP]
    init.append(f"{var}->skip = {skip}")
    # Set outer config first
    init.extend(set_obj_properties(page, config))
    init.extend(set_obj_properties(page, pconf))
    if CONF_WIDGETS in pconf:
        for widg in pconf[CONF_WIDGETS]:
            (_, ext_init) = await widget_to_code(widg, page)
            init.extend(ext_init)
    return var, init


async def switch_to_code(var, btn):
    return []


async def btn_to_code(var, btn):
    return []


async def led_to_code(var, config):
    init = []
    if color := config.get(CONF_COLOR):
        init.append(f"lv_led_set_color({var}, {color})")
    if brightness := await get_value_expr(config.get(CONF_BRIGHTNESS)):
        init.append(f"lv_led_set_brightness({var}, {brightness} * 255)")
    return init


SHOW_SCHEMA = LVGL_SCHEMA.extend(
    {
        cv.Optional(CONF_ANIMATION, default="NONE"): lv_one_of(
            LV_ANIM, "LV_SCR_LOAD_ANIM_"
        ),
        cv.Optional(CONF_TIME, default="50ms"): cv.positive_time_period_milliseconds,
    }
)


@automation.register_action(
    "lvgl.page.next",
    ObjUpdateAction,
    SHOW_SCHEMA,
)
async def page_next_to_code(config, action_id, template_arg, args):
    lv_comp = await cg.get_variable(config[CONF_LVGL_ID])
    animation = config[CONF_ANIMATION]
    time = config[CONF_TIME].milliseconds
    init = [f"{lv_comp}->show_next_page(false, {animation}, {time})"]
    return await action_to_code(init, action_id, lv_comp, template_arg)


@automation.register_action(
    "lvgl.page.previous",
    ObjUpdateAction,
    SHOW_SCHEMA,
)
async def page_previous_to_code(config, action_id, template_arg, args):
    lv_comp = await cg.get_variable(config[CONF_LVGL_ID])
    animation = config[CONF_ANIMATION]
    time = config[CONF_TIME].milliseconds
    init = [f"{lv_comp}->show_next_page(true, {animation}, {time})"]
    return await action_to_code(init, action_id, lv_comp, template_arg)


@automation.register_action(
    "lvgl.page.show",
    ObjUpdateAction,
    cv.maybe_simple_value(
        SHOW_SCHEMA.extend(
            {
                cv.Required(CONF_ID): cv.use_id(lv_page_t),
            }
        ),
        key=CONF_ID,
    ),
)
async def page_show_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    lv_comp = await cg.get_variable(config[CONF_LVGL_ID])
    animation = config[CONF_ANIMATION]
    time = config[CONF_TIME].milliseconds
    init = [f"{lv_comp}->show_page({obj}->index, {animation}, {time})"]
    return await action_to_code(init, action_id, obj, template_arg)


@automation.register_action(
    "lvgl.led.update",
    ObjUpdateAction,
    modify_schema(CONF_LED),
)
async def led_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = await led_to_code(obj, config)
    return await update_to_code(config, action_id, obj, init, template_arg)


async def roller_to_code(var, config):
    init = []
    mode = config[CONF_MODE]
    if options := config.get(CONF_OPTIONS):
        text = cg.safe_exp("\n".join(options))
        init.append(f"lv_roller_set_options({var}, {text}, {mode})")
    animated = config.get(CONF_ANIMATED) or "LV_ANIM_OFF"
    if selected := config.get(CONF_SELECTED_INDEX):
        value = await get_value_expr(selected, cg.uint16)
        init.append(f"lv_roller_set_selected({var}, {value}, {animated})")
    return init


@automation.register_action(
    "lvgl.roller.update",
    ObjUpdateAction,
    modify_schema(CONF_ROLLER),
)
async def roller_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = await roller_to_code(obj, config)
    return await update_to_code(config, action_id, obj, init, template_arg)


async def dropdown_to_code(var, config):
    init = []
    if options := config.get(CONF_OPTIONS):
        text = cg.safe_exp("\n".join(options))
        init.append(f"lv_dropdown_set_options({var}, {text})")
    if symbol := config.get(CONF_SYMBOL):
        init.append(f"lv_dropdown_set_symbol({var}, {symbol})")
    if selected := config.get(CONF_SELECTED_INDEX):
        value = await get_value_expr(selected, cg.uint16)
        init.append(f"lv_dropdown_set_selected({var}, {value})")
    if dir := config.get(CONF_DIR):
        init.append(f"lv_dropdown_set_dir({var}, {dir})")
    if list := config.get(CONF_DROPDOWN_LIST):
        s = f"{var}__list"
        init.extend(add_temp_var("lv_obj_t", s))
        init.append(f"{s} = lv_dropdown_get_list({var});")
        init.extend(set_obj_properties(s, list))
    return init


@automation.register_action(
    "lvgl.dropdown.update",
    ObjUpdateAction,
    modify_schema(CONF_DROPDOWN),
)
async def dropdown_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = await dropdown_to_code(obj, config)
    return await update_to_code(config, action_id, obj, init, template_arg)


# Track mapping arrays by component
btnm_comp_list = {}


def get_btn_generator(id):
    while True:
        try:
            return CONF_BTN, btnm_comp_list[id]
        except KeyError:
            try:
                return CONF_OBJ, CORE.variables[id]
            except KeyError:
                yield


async def get_matrix_button(id: ID):
    # Fast path, check if already registered without awaiting
    if obj := CORE.variables.get(id):
        return CONF_OBJ, obj
    if obj := btnm_comp_list.get(id):
        return CONF_BTN, obj
    return await FakeAwaitable(get_btn_generator(id))


async def btnmatrix_to_code(btnm, conf):
    text_list = []
    ctrl_list = []
    width_list = []
    id = conf[CONF_ID]
    for row in conf[CONF_ROWS]:
        for btn in row[CONF_BUTTONS]:
            bid = btn[CONF_ID]
            btnm_comp_list[bid] = (btnm, len(width_list), btn)
            if text := btn.get(CONF_TEXT):
                text_list.append(f"{cg.safe_exp(text)}")
            elif text := btn.get(CONF_SYMBOL):
                text_list.append(text)
            else:
                text_list.append("")
            width_list.append(btn[CONF_WIDTH])
            ctrl = ["(int)LV_BTNMATRIX_CTRL_CLICK_TRIG"]
            if controls := btn.get(CONF_CONTROL):
                for item in controls:
                    ctrl.extend(
                        [
                            f"(int)LV_BTNMATRIX_CTRL_{k.upper()}"
                            for k, v in item.items()
                            if v
                        ]
                    )
            ctrl_list.append("|".join(ctrl))
        text_list.append('"\\n"')
    text_list = text_list[:-1]
    text_list.append("NULL")
    text_list = cg.RawExpression("{" + ",".join(text_list) + "}")
    text_id = ID(f"{id.id}_text_array", is_declaration=True, type=char_ptr_const)
    text_id = cg.static_const_array(text_id, text_list)
    init = [f"lv_btnmatrix_set_map({btnm}, {text_id})"]
    for index, ctrl in enumerate(ctrl_list):
        init.append(f"lv_btnmatrix_set_btn_ctrl({btnm}, {index}, {ctrl})")
    for index, width in enumerate(width_list):
        init.append(f"lv_btnmatrix_set_btn_width({btnm}, {index}, {width})")
    return init


async def img_to_code(var, img):
    return [f"lv_img_set_src({var}, lv_img_from({img[CONF_SRC]}))"]


async def line_to_code(var, line):
    """For a line object, create and add the points"""
    data = line[CONF_POINTS]
    point_list = data[CONF_POINTS]
    initialiser = cg.RawExpression(
        "{" + ",".join(map(lambda p: "{" + f"{p[0]}, {p[1]}" + "}", point_list)) + "}"
    )
    points = cg.static_const_array(data[CONF_ID], initialiser)
    return [f"lv_line_set_points({var}, {points}, {len(point_list)})"]


meter_indicators = {}


async def meter_to_code(var, meter):
    """For a meter object, create and set parameters"""

    init = []
    s = "meter_var"
    init.extend(add_temp_var("lv_meter_scale_t", s))
    for scale in meter.get(CONF_SCALES) or ():
        rotation = 90 + (360 - scale[CONF_ANGLE_RANGE]) / 2
        if CONF_ROTATION in scale:
            rotation = scale[CONF_ROTATION]
        init.append(f"{s} = lv_meter_add_scale({var})")
        init.append(
            f"lv_meter_set_scale_range({var}, {s}, {scale[CONF_RANGE_FROM]},"
            + f"{scale[CONF_RANGE_TO]}, {scale[CONF_ANGLE_RANGE]}, {rotation})",
        )
        if ticks := scale.get(CONF_TICKS):
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
        for indicator in scale.get(CONF_INDICATORS) or ():
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
            start_value = await get_start_value(v)
            end_value = await get_end_value(v)
            if start_value is not None:
                init.append(
                    f"lv_meter_set_indicator_start_value({var}, {ivar}, {start_value})"
                )
            if end_value is not None:
                init.append(
                    f"lv_meter_set_indicator_end_value({var}, {ivar}, {end_value})"
                )

    return init


async def arc_to_code(var, arc):
    init = [
        f"lv_arc_set_range({var}, {arc[CONF_MIN_VALUE]}, {arc[CONF_MAX_VALUE]})",
        f"lv_arc_set_bg_angles({var}, {arc[CONF_START_ANGLE]}, {arc[CONF_END_ANGLE]})",
        f"lv_arc_set_rotation({var}, {arc[CONF_ROTATION]})",
        f"lv_arc_set_mode({var}, {arc[CONF_MODE]})",
        f"lv_arc_set_change_rate({var}, {arc[CONF_CHANGE_RATE]})",
    ]
    value = await get_start_value(arc)
    if value is not None:
        init.append(f"lv_arc_set_value({var}, {value})")
    return init


async def bar_to_code(var, conf):
    init = [
        f"lv_bar_set_range({var}, {conf[CONF_MIN_VALUE]}, {conf[CONF_MAX_VALUE]})",
        f"lv_bar_set_mode({var}, {conf[CONF_MODE]})",
    ]
    value = await get_start_value(conf)
    if value is not None:
        init.append(f"lv_bar_set_value({var}, {value}, LV_ANIM_OFF)")
    return init


async def rotary_encoders_to_code(var, config):
    init = []
    if CONF_ROTARY_ENCODERS not in config:
        return init
    lv_uses.add("ROTARY_ENCODER")
    for enc_conf in config[CONF_ROTARY_ENCODERS]:
        sensor = await cg.get_variable(enc_conf[CONF_SENSOR])
        lpt = enc_conf[CONF_LONG_PRESS_TIME].milliseconds
        lprt = enc_conf[CONF_LONG_PRESS_REPEAT_TIME].milliseconds
        listener = cg.new_Pvariable(enc_conf[CONF_ID], lpt, lprt)
        await cg.register_parented(listener, var)
        if group := add_group(enc_conf.get(CONF_GROUP)):
            init.append(
                f"lv_indev_set_group(lv_indev_drv_register(&{listener}->drv), {group})"
            )
        else:
            init.append(f"lv_indev_drv_register(&{listener}->drv)")
        init.append(
            f"{sensor}->register_listener([](uint32_t count) {{ {listener}->set_count(count); }})",
        )
        if b_sensor := enc_conf.get(CONF_BINARY_SENSOR):
            b_sensor = await cg.get_variable(b_sensor)
            init.append(
                f"{b_sensor}->add_on_state_callback([](bool state) {{ {listener}->set_pressed(state); }})"
            )
        return init


async def touchscreens_to_code(var, config):
    init = []
    if CONF_TOUCHSCREENS not in config:
        return init
    lv_uses.add("TOUCHSCREEN")
    for touchconf in config[CONF_TOUCHSCREENS]:
        touchscreen = await cg.get_variable(touchconf[CONF_TOUCHSCREEN_ID])
        lpt = touchconf[CONF_LONG_PRESS_TIME].milliseconds
        lprt = touchconf[CONF_LONG_PRESS_REPEAT_TIME].milliseconds
        listener = cg.new_Pvariable(touchconf[CONF_ID], lpt, lprt)
        await cg.register_parented(listener, var)
        init.extend(
            [
                f"lv_indev_drv_register(&{listener}->drv)",
                f"{touchscreen}->register_listener({listener})",
            ]
        )
    return init


async def generate_triggers():
    init = []
    for obj, wconf in widget_list:
        for event, conf in {
            event: conf for event, conf in wconf.items() if event in LV_EVENT_TRIGGERS
        }.items():
            event = LV_EVENT[event[3:].upper()]
            conf = conf[0]
            trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
            await automation.build_automation(trigger, [], conf)
            init.append(
                f"""
            lv_obj_add_flag({obj}, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb({obj}, [](lv_event_t *ev) {{
                {trigger}->trigger();
            }}, LV_EVENT_{event.upper()}, nullptr)
            """
            )

    for obj, pair in btnm_comp_list.items():
        bconf = pair[2]
        index = pair[1]
        btnm = pair[0]
        for event, conf in {
            event: conf for event, conf in bconf.items() if event in LV_EVENT_TRIGGERS
        }.items():
            event = LV_EVENT[event[3:].upper()]
            conf = conf[0]
            trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
            await automation.build_automation(trigger, [], conf)
            init.append(
                f"""
            lv_obj_add_event_cb({btnm}, [](lv_event_t *ev) {{
                if (lv_btnmatrix_get_selected_btn({btnm}) == {index})
                    {trigger}->trigger();
            }}, LV_EVENT_{event.upper()}, nullptr)
            """
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
    CORE.add_build_flag("-Isrc")

    cg.add_global(lvgl_ns.using)
    lv_component = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(lv_component, config)
    if display := config.get(CONF_DISPLAY_ID):
        cg.add(lv_component.add_display(await cg.get_variable(display)))
    for display in config.get(CONF_DISPLAYS, []):
        cg.add(
            lv_component.add_display(await cg.get_variable(display[CONF_DISPLAY_ID]))
        )
    frac = config[CONF_BUFFER_SIZE]
    if frac >= 0.75:
        frac = 1
    elif frac >= 0.375:
        frac = 2
    elif frac > 0.19:
        frac = 4
    else:
        frac = 8
    cg.add(lv_component.set_buffer_frac(int(frac)))
    cgen("lv_init()")
    if CONF_ROTARY_ENCODERS in config:  # or CONF_KEYBOARDS in config
        cgen("lv_group_set_default(lv_group_create())")
    init = []
    if style_defs := config.get(CONF_STYLE_DEFINITIONS, []):
        styles_to_code(style_defs)
    # must do this before generating widgets
    if theme := config.get(CONF_THEME):
        await theme_to_code(theme)
    if top_conf := config.get(CONF_TOP_LAYER):
        init.extend(set_obj_properties("lv_disp_get_layer_top(lv_disp)", top_conf))
        if widgets := top_conf.get(CONF_WIDGETS):
            for widg in widgets:
                (_, ext_init) = await widget_to_code(
                    widg, "lv_disp_get_layer_top(lv_disp)"
                )
                init.extend(ext_init)
    if widgets := config.get(CONF_WIDGETS):
        init.extend(set_obj_properties("lv_scr_act()", config))
        for widg in widgets:
            (_, ext_init) = await widget_to_code(widg, "lv_scr_act()")
            init.extend(ext_init)
    if pages := config.get(CONF_PAGES):
        for index, pconf in enumerate(pages):
            pvar, pinit = await page_to_code(config, pconf, index)
            init.append(f"{lv_component}->add_page({pvar})")
            init.extend(pinit)

    init.extend(await generate_triggers())
    init.extend(await touchscreens_to_code(lv_component, config))
    init.extend(await rotary_encoders_to_code(lv_component, config))
    init.extend(set_obj_properties("lv_scr_act()", config))
    if on_idle := config.get(CONF_ON_IDLE):
        for conf in on_idle:
            templ = await cg.templatable(conf[CONF_TIMEOUT], [], cg.uint32)
            trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], lv_component, templ)
            await automation.build_automation(trigger, [], conf)

    await add_init_lambda(lv_component, init)
    for use in lv_uses:
        CORE.add_build_flag(f"-DLV_USE_{use.upper()}=1")
    for macro, value in lv_defines.items():
        cg.add_build_flag(f"-D\\'{macro}\\'=\\'{value}\\'")


def indicator_update_schema(base):
    return base.extend({cv.Required(CONF_ID): cv.use_id(lv_meter_indicator_t)})


async def action_to_code(action, action_id, obj, template_arg):
    action.insert(0, f"if ({obj} == nullptr) return")
    lamb = await cg.process_lambda(Lambda(";\n".join([*action, ""])), [])
    var = cg.new_Pvariable(action_id, template_arg, lamb)
    return var


async def update_to_code(config, action_id, obj, init, template_arg):
    if config is not None:
        init.extend(set_obj_properties(obj, config))
    return await action_to_code(init, action_id, obj, template_arg)


CONFIG_SCHEMA = (
    cv.polling_component_schema("1s")
    .extend(obj_schema())
    .extend(
        {
            cv.Optional(CONF_ID, default=CONF_LVGL_COMPONENT): cv.declare_id(
                LvglComponent
            ),
            cv.Exclusive(CONF_DISPLAYS, CONF_DISPLAY_ID): cv.ensure_list(
                cv.maybe_simple_value(
                    {
                        cv.Required(CONF_DISPLAY_ID): cv.use_id(Display),
                    },
                    key=CONF_DISPLAY_ID,
                )
            ),
            cv.Exclusive(CONF_DISPLAY_ID, CONF_DISPLAY_ID): cv.use_id(Display),
            cv.Optional(CONF_TOUCHSCREENS): cv.ensure_list(
                cv.maybe_simple_value(
                    {
                        cv.Required(CONF_TOUCHSCREEN_ID): cv.use_id(Touchscreen),
                        cv.Optional(
                            CONF_LONG_PRESS_TIME, default="400ms"
                        ): cv.positive_time_period_milliseconds,
                        cv.Optional(
                            CONF_LONG_PRESS_REPEAT_TIME, default="100ms"
                        ): cv.positive_time_period_milliseconds,
                        cv.GenerateID(): cv.declare_id(LVTouchListener),
                    },
                    key=CONF_TOUCHSCREEN_ID,
                )
            ),
            cv.Optional(CONF_ROTARY_ENCODERS): cv.All(
                cv.ensure_list(
                    cv.Schema(
                        {
                            cv.Required(CONF_SENSOR): cv.use_id(RotaryEncoderSensor),
                            cv.Optional(
                                CONF_LONG_PRESS_TIME, default="400ms"
                            ): cv.positive_time_period_milliseconds,
                            cv.Optional(
                                CONF_LONG_PRESS_REPEAT_TIME, default="100ms"
                            ): cv.positive_time_period_milliseconds,
                            cv.Optional(CONF_BINARY_SENSOR): cv.use_id(BinarySensor),
                            cv.Optional(CONF_GROUP): lv_id_name,
                            cv.GenerateID(): cv.declare_id(LVRotaryEncoderListener),
                        }
                    )
                ),
            ),
            cv.Optional(CONF_COLOR_DEPTH, default=16): cv.one_of(1, 8, 16, 32),
            cv.Optional(CONF_BUFFER_SIZE, default="100%"): cv.percentage,
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
                    cv.Required(CONF_TIMEOUT): cv.templatable(
                        cv.positive_time_period_milliseconds
                    ),
                }
            ),
            cv.Exclusive(CONF_WIDGETS, CONF_PAGES): cv.ensure_list(WIDGET_SCHEMA),
            cv.Exclusive(CONF_PAGES, CONF_PAGES): cv.ensure_list(
                container_schema(CONF_PAGE)
            ),
            cv.Optional(CONF_TOP_LAYER): container_schema(CONF_OBJ),
            cv.Optional(CONF_THEME): cv.Schema(
                {cv.Optional(w): obj_schema(w) for w in WIDGET_TYPES}
            ).extend({cv.Optional(CONF_PAGE): obj_schema(CONF_PAGE)}),
        }
    )
).add_extra(cv.has_at_least_one_key(CONF_PAGES, CONF_WIDGETS))


async def widget_to_code(widget, parent):
    (w_type, w_cnfig) = next(iter(widget.items()))
    lv_uses.add(w_type)
    (var, init) = await create_lv_obj(w_type, w_cnfig, parent)
    fun = f"{w_type}_to_code"
    if fun in globals():
        fun = globals()[fun]
        init.extend(await fun(var, w_cnfig))
    else:
        raise cv.Invalid(f"No handler for widget {w_type}")
    widget_list.append(
        (
            var,
            w_cnfig,
        )
    )
    return var, init


ACTION_SCHEMA = cv.maybe_simple_value(
    {
        cv.Required(CONF_ID): cv.use_id(lv_pseudo_button_t),
    },
    key=CONF_ID,
)


@automation.register_action("lvgl.widget.disable", ObjUpdateAction, ACTION_SCHEMA)
async def obj_disable_to_code(config, action_id, template_arg, args):
    otype, obj = await get_matrix_button(config[CONF_ID])
    if otype == CONF_OBJ:
        action = [f"lv_obj_add_state({obj}, LV_STATE_DISABLED)"]
    else:
        idx = obj[1]
        obj = obj[0]
        action = [
            f"lv_btnmatrix_add_btn_ctrl({obj}, {idx}, LV_BTNMATRIX_CTRL_DISABLED)"
        ]
    return await action_to_code(action, action_id, obj, template_arg)


@automation.register_action("lvgl.widget.enable", ObjUpdateAction, ACTION_SCHEMA)
async def obj_enable_to_code(config, action_id, template_arg, args):
    otype, obj = await get_matrix_button(config[CONF_ID])
    if otype == CONF_OBJ:
        action = [f"lv_obj_clear_state({obj}, LV_STATE_DISABLED)"]
    else:
        idx = obj[1]
        obj = obj[0]
        action = [
            f"lv_btnmatrix_clear_btn_ctrl({obj}, {idx}, LV_BTNMATRIX_CTRL_DISABLED)"
        ]
    return await action_to_code(action, action_id, obj, template_arg)


@automation.register_action("lvgl.widget.show", ObjUpdateAction, ACTION_SCHEMA)
async def obj_show_to_code(config, action_id, template_arg, args):
    otype, obj = await get_matrix_button(config[CONF_ID])
    if otype == CONF_OBJ:
        action = [f"lv_obj_clear_flag({obj}, LV_OBJ_FLAG_HIDDEN)"]
    else:
        idx = obj[1]
        obj = obj[0]
        action = [
            f"lv_btnmatrix_clear_btn_ctrl({obj}, {idx}, LV_BTNMATRIX_CTRL_HIDDEN)"
        ]
    return await action_to_code(action, action_id, obj, template_arg)


@automation.register_action("lvgl.widget.hide", ObjUpdateAction, ACTION_SCHEMA)
async def obj_hide_to_code(config, action_id, template_arg, args):
    otype, obj = await get_matrix_button(config[CONF_ID])
    if otype == CONF_OBJ:
        action = [f"lv_obj_add_flag({obj}, LV_OBJ_FLAG_HIDDEN)"]
    else:
        idx = obj[1]
        obj = obj[0]
        action = [f"lv_btnmatrix_set_btn_ctrl({obj}, {idx}, LV_BTNMATRIX_CTRL_HIDDEN)"]
    return await action_to_code(action, action_id, obj, template_arg)


@automation.register_action(
    "lvgl.widget.update", ObjUpdateAction, modify_schema(CONF_OBJ)
)
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
    init = await checkbox_to_code(obj, config)
    return await update_to_code(config, action_id, obj, init, template_arg)


@automation.register_action(
    "lvgl.label.update",
    ObjUpdateAction,
    modify_schema(CONF_LABEL),
)
async def label_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = await label_to_code(obj, config)
    return await update_to_code(config, action_id, obj, init, template_arg)


@automation.register_action(
    "lvgl.indicator.line.update",
    ObjUpdateAction,
    indicator_update_schema(INDICATOR_LINE_SCHEMA),
)
async def indicator_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    meter = meter_indicators[config[CONF_ID]]
    init = []
    start_value = await get_start_value(config)
    end_value = await get_end_value(config)
    selector = "start_" if end_value is not None else ""
    if start_value is not None:
        init.append(
            f"lv_meter_set_indicator_{selector}value({meter},{obj}, {start_value})"
        )
    if end_value is not None:
        init.append(f"lv_meter_set_indicator_end_value({meter},{obj}, {end_value})")
    return await update_to_code(None, action_id, obj, init, template_arg)


@automation.register_action(
    "lvgl.button.update",
    ObjUpdateAction,
    cv.Schema(
        {
            cv.Optional(CONF_WIDTH, default=1): cv.positive_int,
            cv.Optional(CONF_CONTROL): cv.ensure_list(
                cv.Schema(
                    {cv.Optional(k.lower()): cv.boolean for k in BTNMATRIX_CTRLS}
                ),
            ),
            cv.Required(CONF_ID): cv.use_id(LvBtnmBtn),
            cv.Optional(CONF_SELECTED): lv_bool,
        }
    ),
)
async def button_update_to_code(config, action_id, template_arg, args):
    _, obj = await get_matrix_button(config[CONF_ID])
    index = obj[1]
    btnm = obj[0]
    init = []
    if (width := config.get(CONF_WIDTH)) is not None:
        init.append(f"lv_btnmatrix_set_btn_width({btnm}, {index}, {width})")
    if config.get(CONF_SELECTED):
        init.append(f"lv_btnmatrix_set_selected_btn({btnm}, {index})")
    if controls := config.get(CONF_CONTROL):
        ctrl = ["(int)LV_BTNMATRIX_CTRL_CLICK_TRIG"]
        for item in controls:
            ctrl.extend(
                [f"(int)LV_BTNMATRIX_CTRL_{k.upper()}" for k, v in item.items() if v]
            )
        controls = "|".join(ctrl)
        init.append(f"lv_btnmatrix_set_btn_ctrl({btnm}, {index}, {controls})")

    return await action_to_code(init, action_id, btnm, template_arg)


@automation.register_action(
    "lvgl.arc.update",
    ObjUpdateAction,
    modify_schema(CONF_ARC),
)
async def arc_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = []
    value = await get_value_expr(config.get(CONF_VALUE))
    init.append(f"lv_arc_set_value({obj}, {value})")
    return await update_to_code(config, action_id, obj, init, template_arg)


async def slider_to_code(var, slider):
    init = [
        f"lv_slider_set_range({var}, {slider[CONF_MIN_VALUE]}, {slider[CONF_MAX_VALUE]})",
        f"lv_slider_set_mode({var}, {slider[CONF_MODE]})",
    ]
    value = await get_start_value(slider)
    if value is not None:
        init.append(f"lv_slider_set_value({var}, {value}, LV_ANIM_OFF)")
    return init


@automation.register_action(
    "lvgl.slider.update",
    ObjUpdateAction,
    modify_schema(CONF_SLIDER),
)
async def slider_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = []
    animated = config[CONF_ANIMATED]
    value = await get_value_expr(config.get(CONF_VALUE), cg.uint32)
    init.append(f"lv_slider_set_value({obj}, {value}, {animated})")
    return await update_to_code(config, action_id, obj, init, template_arg)


@automation.register_action(
    "lvgl.img.update",
    ObjUpdateAction,
    modify_schema(CONF_IMG),
)
async def img_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = []
    if src := config.get(CONF_SRC):
        init.append(f"lv_img_set_src({obj}, lv_img_from({src}))")
    return await update_to_code(config, action_id, obj, init, template_arg)


@automation.register_action(
    "lvgl.widget.redraw",
    LvglAction,
    cv.Schema(
        {
            cv.Optional(CONF_ID): cv.use_id(lv_obj_t),
            cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
        }
    ),
)
async def obj_invalidate_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_LVGL_ID])
    if obj_id := config.get(CONF_ID):
        obj = cg.get_variable(obj_id)
    else:
        obj = "lv_scr_act()"
    lamb = await cg.process_lambda(
        Lambda(f"lv_obj_invalidate({obj});"), [(LvglComponentPtr, "lvgl_comp")]
    )
    cg.add(var.set_action(lamb))
    return var


@automation.register_action(
    "lvgl.pause",
    LvglAction,
    {
        cv.GenerateID(): cv.use_id(LvglComponent),
    },
)
async def pause_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    lamb = await cg.process_lambda(
        Lambda("lvgl_comp->set_paused(true);"), [(LvglComponentPtr, "lvgl_comp")]
    )
    cg.add(var.set_action(lamb))
    return var


@automation.register_action(
    "lvgl.resume",
    LvglAction,
    {
        cv.GenerateID(): cv.use_id(LvglComponent),
    },
)
async def resume_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    lamb = await cg.process_lambda(
        Lambda("lvgl_comp->set_paused(false);"), [(LvglComponentPtr, "lvgl_comp")]
    )
    cg.add(var.set_action(lamb))
    return var


@automation.register_condition(
    "lvgl.is_idle",
    LvglCondition,
    LVGL_SCHEMA.extend(
        {
            cv.Required(CONF_TIMEOUT): cv.templatable(
                cv.positive_time_period_milliseconds
            )
        }
    ),
)
async def lvgl_is_idle(config, condition_id, template_arg, args):
    var = cg.new_Pvariable(condition_id, template_arg)
    lvgl = config[CONF_LVGL_ID]
    timeout = await cg.templatable(config[CONF_TIMEOUT], [], cg.uint32)
    if isinstance(timeout, LambdaExpression):
        timeout = f"({timeout}())"
    await cg.register_parented(var, lvgl)
    lamb = await cg.process_lambda(
        Lambda(f"return lvgl_comp->is_idle({timeout});"),
        [(LvglComponentPtr, "lvgl_comp")],
    )
    cg.add(var.set_condition_lambda(lamb))
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
    lamb = await cg.process_lambda(
        Lambda("return lvgl_comp->is_paused();"), [(LvglComponentPtr, "lvgl_comp")]
    )
    cg.add(var.set_condition_lambda(lamb))
    return var
