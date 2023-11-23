import sys

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_ID,
    CONF_VALUE,
)
from .. import (
    lvgl_ns,
    LVGL_SCHEMA,
    lv_arc_t,
    lv_slider_t,
    CONF_LVGL_ID,
    add_init_lambda,
    CONF_ANIMATED,
    lv_animated,
    set_event_cb,
)
from ..sensor import CONF_ARC_ID, CONF_SLIDER_ID

LVGLNumber = lvgl_ns.class_("LVGLNumber", number.Number)

CONFIG_SCHEMA = (
    number.number_schema(LVGLNumber)
    .extend(LVGL_SCHEMA)
    .extend(
        {
            cv.Exclusive(CONF_ARC_ID, CONF_VALUE): cv.use_id(lv_arc_t),
            cv.Exclusive(CONF_SLIDER_ID, CONF_VALUE): cv.use_id(lv_slider_t),
            cv.Optional(CONF_ANIMATED, default=True): lv_animated,
        }
    )
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await number.register_number(
        var, config, max_value=sys.maxsize, min_value=-sys.maxsize, step=1
    )

    animated = config[CONF_ANIMATED]
    paren = await cg.get_variable(config[CONF_LVGL_ID])
    if CONF_ARC_ID in config:
        obj = await cg.get_variable(config[CONF_ARC_ID])
        lv_type = "arc"
    elif CONF_SLIDER_ID in config:
        obj = await cg.get_variable(config[CONF_SLIDER_ID])
        lv_type = "slider"
    else:
        return
    init = set_event_cb(
        obj,
        f"   {var}->publish_state(lv_{lv_type}_get_value({obj}));\n",
        "LV_EVENT_VALUE_CHANGED",
        f"{paren}->get_custom_change_event()",
    ) + [
        f"{var}->set_control_lambda([] (float v) {{\n"
        f"  lv_{lv_type}_set_value({obj}, v, {animated});\n"
        "})"
    ]
    init.extend(
        [
            f"{var}->traits.set_max_value(lv_{lv_type}_get_max_value({obj}))",
            f"{var}->traits.set_min_value(lv_{lv_type}_get_min_value({obj}))",
        ]
    )
    await add_init_lambda(paren, init)
