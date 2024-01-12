import esphome.config_validation as cv
from esphome.core import ID
from esphome.schema_extractors import (
    SCHEMA_EXTRACT,
    schema_extractor,
)
from esphome.components.font import Font
from esphome.components.color import ColorStruct
from esphome.components.binary_sensor import BinarySensor
from esphome.components.sensor import Sensor
from esphome.components.text_sensor import TextSensor
from esphome.const import (
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
)
from .defines import (
    LV_FONTS,
    CONF_IMG,
    CONF_ROTARY_ENCODERS,
    CONF_TOUCHSCREENS,
)

lv_uses = {
    "USER_DATA",
    "LOG",
}

lv_fonts_used = set()

REQUIRED_COMPONENTS = {
    CONF_IMG: "image",
    CONF_ROTARY_ENCODERS: "rotary_encoder",
    CONF_TOUCHSCREENS: "touchscreen",
}
# List of other components used
lvgl_components_required = set()


def requires_component(comp):
    def validator(value):
        lvgl_components_required.add(comp)
        return cv.requires_component(comp)(value)

    return validator


@schema_extractor("one_of")
def lv_color(value):
    if value == SCHEMA_EXTRACT:
        return ["hex color value", "color ID"]
    if isinstance(value, int):
        hexval = cv.hex_int(value)
        return f"lv_color_hex({hexval})"
    color_id = cv.use_id(ColorStruct)(value)
    return f"lv_color_from({color_id})"


def lv_font(value):
    """Accept either the name of a built-in LVGL font, or the ID of an ESPHome font"""
    if value == SCHEMA_EXTRACT:
        return LV_FONTS
    if isinstance(value, str) and value.lower() in LV_FONTS:
        font = cv.one_of(*LV_FONTS, lower=True)(value)
        lv_fonts_used.add(font)
        return "&lv_font_" + font
    lv_uses.add("FONT")
    font = cv.use_id(Font)(value)
    return f"(new lvgl::FontEngine({font}))->get_lv_font()"


@schema_extractor("one_of")
def lv_bool(value):
    if value == SCHEMA_EXTRACT:
        return ["true", "false"]
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


def join_enums(enums, prefix=""):
    return "|".join(map(lambda e: f"(int){prefix}{e.upper()}", enums))


def lv_any_of(choices, prefix):
    """Allow any of a list of choices, mapped to upper case, and prepend the choice with the prefix.
    It's also permitted to include the prefix in the value"""

    @schema_extractor("one_of")
    def validator(value):
        if value == SCHEMA_EXTRACT:
            return choices
        if not isinstance(value, list):
            value = [value]
        return "|".join(map(lambda v: "(int)" + lv_prefix(v, choices, prefix), value))

    return validator


def lv_id_name(value):
    if value == SCHEMA_EXTRACT:
        return "id"
    return cv.validate_id_name(value)


def pixels_or_percent(value):
    """A length in one axis - either a number (pixels) or a percentage"""
    if value == SCHEMA_EXTRACT:
        return ["pixels", "..%"]
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
    if isinstance(value, str) and value.lower().endswith("px"):
        value = cv.int_(value[:-2])
    if isinstance(value, str) and not value.endswith("%"):
        if value.upper() == "SIZE_CONTENT":
            return "LV_SIZE_CONTENT"
        raise cv.Invalid("must be 'size_content', a pixel position or a percentage")
    if isinstance(value, int):
        return str(cv.int_(value))
    # Will throw an exception if not a percentage.
    return f"lv_pct({int(cv.percentage(value) * 100)})"


@schema_extractor("one_of")
def lv_opacity(value):
    if value == SCHEMA_EXTRACT:
        return ["TRANSP", "COVER", "..%"]
    value = cv.Any(cv.percentage, lv_one_of(["TRANSP", "COVER"], "LV_OPA_"))(value)
    if isinstance(value, str):
        return value
    return int(value * 255)


def lv_stop_value(value):
    return cv.int_range(0, 255)(value)


def lv_value(value, validators=None):
    if isinstance(value, int):
        return cv.float_(float(cv.int_(value)))
    if isinstance(value, float):
        return cv.float_(value)
    return cv.templatable(cv.use_id(Sensor))(value)


def lv_text_value(value):
    if isinstance(value, cv.Lambda):
        return cv.returning_lambda(value)
    if isinstance(value, ID):
        return cv.use_id(TextSensor)(value)
    return cv.string(value)


def lv_boolean_value(value):
    if isinstance(value, cv.Lambda):
        return cv.returning_lambda(value)
    if isinstance(value, ID):
        return cv.use_id(BinarySensor)(value)
    return "true" if cv.boolean(value) else "false"


def optional_boolean(value):
    if value is None:
        return True
    return cv.boolean(value)


def validate_max_min(config):
    if CONF_MAX_VALUE in config and CONF_MIN_VALUE in config:
        if config[CONF_MAX_VALUE] <= config[CONF_MIN_VALUE]:
            raise cv.Invalid("max_value must be greater than min_value")
    return config


def cv_int_list(il):
    nl = il.replace(" ", "").split(",")
    return list(map(int, nl))


def lv_option_string(value):
    value = cv.string(value).strip()
    if value.find("\n") != -1:
        raise cv.Invalid("Options strings must not contain newlines")
    return value
