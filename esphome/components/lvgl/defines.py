# Widgets
CONF_ARC = "arc"
CONF_BAR = "bar"
CONF_BTN = "btn"
CONF_BTNMATRIX = "btnmatrix"
CONF_CANVAS = "canvas"
CONF_CHECKBOX = "checkbox"
CONF_DROPDOWN = "dropdown"
CONF_IMG = "img"
CONF_LABEL = "label"
CONF_LINE = "line"
CONF_METER = "meter"
CONF_ROLLER = "roller"
CONF_SLIDER = "slider"
CONF_SWITCH = "switch"
CONF_TABLE = "table"
CONF_TEXTAREA = "textarea"

# Parts
CONF_MAIN = "main"
CONF_SCROLLBAR = "scrollbar"
CONF_INDICATOR = "indicator"
CONF_KNOB = "knob"
CONF_SELECTED = "selected"
CONF_ITEMS = "items"
CONF_TICKS = "ticks"
CONF_CURSOR = "cursor"
CONF_TEXTAREA_PLACEHOLDER = "textarea_placeholder"

LOG_LEVELS = (
    "TRACE",
    "INFO",
    "WARN",
    "ERROR",
    "USER",
    "NONE",
)
STATES = (
    "default",
    "checked",
    "focused",
    "focus_key",
    "edited",
    "hovered",
    "pressed",
    "scrolled",
    "disabled",
    "user_1",
    "user_2",
    "user_3",
    "user_4",
)

PARTS = [
    CONF_MAIN,
    CONF_SCROLLBAR,
    CONF_INDICATOR,
    CONF_KNOB,
    CONF_SELECTED,
    CONF_ITEMS,
    CONF_TICKS,
    CONF_CURSOR,
    CONF_TEXTAREA_PLACEHOLDER,
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
BAR_MODES = ["NORMAL", "SYMMETRICAL", "RANGE"]

BTNMATRIX_CTRLS = (
    "NO_REPEAT",
    "DISABLED",
    "CHECKABLE",
    "CHECKED",
    "CLICK_TRIG",
    "POPOVER",
    "RECOLOR",
    "CUSTOM_1",
    "CUSTOM_2",
)
