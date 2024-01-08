import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.switch import (
    switch_schema,
    Switch,
    new_switch,
)
from esphome.const import CONF_EVENT
from . import (
    add_init_lambda,
    LVGL_SCHEMA,
    CONF_LVGL_ID,
    lv_one_of,
    CONF_BTN,
    CONF_CHECKBOX,
    lv_btn_t,
    lv_checkbox_t,
    CONF_OBJ,
    lv_obj_t,
    lvgl_ns,
    CONF_SWITCH,
    lv_switch_t,
)
from .. import switch

LVGLSwitch = lvgl_ns.class_("LVGLSwitch", switch.Switch)
BASE_SCHEMA = switch_schema(Switch).extend(LVGL_SCHEMA)
CONFIG_SCHEMA = cv.Any(
    BASE_SCHEMA.extend(
        {
            cv.Required(CONF_BTN): cv.use_id(lv_btn_t),
        }
    ),
    BASE_SCHEMA.extend(
        {
            cv.Required(CONF_CHECKBOX): cv.use_id(lv_checkbox_t),
        }
    ),
    BASE_SCHEMA.extend(
        {
            cv.Required(CONF_SWITCH): cv.use_id(lv_switch_t),
        }
    ),
)


async def to_code(config):
    sensor = await new_switch(config)
    id = config[CONF_OBJ]
    obj = await cg.get_variable(id)
    paren = await cg.get_variable(config[CONF_LVGL_ID])
    init = [
        f"lv_obj_add_event_cb({obj}, [](lv_event_t *e) {{{sensor}->publish_state(true); }}\n",
        f", LV_VALUE_CHANGED, nullptr)",
        f"lv_obj_add_event_cb({obj}, [](lv_event_t *e) {{{sensor}->publish_state(false);}}\n"
        "}, LV_EVENT_RELEASED, nullptr)",
    ]
    await add_init_lambda(paren, init)
