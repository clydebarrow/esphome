import esphome.codegen as cg
from esphome.const import CONF_OUTPUT_ID
from esphome.components.output import BinaryOutput
import esphome.config_validation as cv
from esphome.components.switch import (
    switch_schema,
    Switch,
    new_switch,
)
from . import (
    add_init_lambda,
    LVGL_SCHEMA,
    CONF_LVGL_ID,
    CONF_BTN,
    lvgl_ns,
    lv_pseudo_button_t,
    get_matrix_button,
    set_event_cb,
    CONF_WIDGET,
)
from .lv_validation import requires_component

AUTO_LOAD = ["output"]

LVGLSwitch = lvgl_ns.class_("LVGLSwitch", Switch)
BASE_SCHEMA = switch_schema(LVGLSwitch).extend(LVGL_SCHEMA)
CONFIG_SCHEMA = cv.All(
    BASE_SCHEMA.extend(
        {
            cv.Required(CONF_WIDGET): cv.use_id(lv_pseudo_button_t),
            cv.Optional(CONF_OUTPUT_ID): cv.use_id(BinaryOutput),
        }
    ),
    requires_component("switch"),
)


async def to_code(config):
    switch = await new_switch(config)
    paren = await cg.get_variable(config[CONF_LVGL_ID])
    (otype, obj) = await get_matrix_button(config[CONF_WIDGET])
    output = config.get(CONF_OUTPUT_ID)
    if otype == CONF_BTN:
        # map the button ID to the button matrix and an index
        idx = obj[1]
        obj = obj[0]
        publish = f"""if (lv_btnmatrix_get_selected_btn({obj}) == {idx}) {{
            bool v = lv_btnmatrix_has_btn_ctrl({obj}, {idx}, LV_BTNMATRIX_CTRL_CHECKED);
            {switch}->publish_state(v);
            """
        if output:
            publish += f"{output}->set_state(v);"
        publish += "}"
        set_state = f"""
            if (v) lv_btnmatrix_set_btn_ctrl({obj}, {idx}, LV_BTNMATRIX_CTRL_CHECKED);
            else lv_btnmatrix_clear_btn_ctrl({obj}, {idx}, LV_BTNMATRIX_CTRL_CHECKED);
            """
    else:
        publish = f"{switch}->publish_state((lv_obj_get_state({obj}) & LV_STATE_CHECKED) != 0)"
        set_state = f"""
            if (v) lv_obj_add_state({obj}, LV_STATE_CHECKED);
            else lv_obj_clear_state({obj}, LV_STATE_CHECKED);
            """
    init = set_event_cb(
        obj,
        publish,
        "LV_EVENT_VALUE_CHANGED",
    )
    set_state += f"{switch}->publish_state(v);"
    if output:
        set_state += f"{output}->set_state(v); "
    init.append(f"{switch}->set_state_lambda([] (bool v) {{\n" + set_state + "\n})")
    await add_init_lambda(paren, init)
