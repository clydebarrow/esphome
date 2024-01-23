import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import (
    CONF_ID,
    CONF_VALUE,
)
from . import (
    lvgl_ns,
    LVGL_SCHEMA,
    CONF_LVGL_ID,
    add_init_lambda,
    set_event_cb,
    CONF_DROPDOWN,
    lv_dropdown_t,
    lv_roller_t,
    CONF_ROLLER,
)
from .lv_validation import requires_component

LVGLSelect = lvgl_ns.class_("LVGLSelect", select.Select)

CONFIG_SCHEMA = cv.All(
    select.select_schema(LVGLSelect)
    .extend(LVGL_SCHEMA)
    .extend(
        {
            cv.Exclusive(CONF_DROPDOWN, CONF_VALUE): cv.use_id(lv_dropdown_t),
            cv.Exclusive(CONF_ROLLER, CONF_VALUE): cv.use_id(lv_roller_t),
        }
    ),
    requires_component("select"),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await select.register_select(var, config, options=[])
    paren = await cg.get_variable(config[CONF_LVGL_ID])
    init = []
    if obj := config.get(CONF_DROPDOWN):
        animated = ""
        obj = await cg.get_variable(obj)
        otype = "dropdown"
    else:
        animated = ", LV_ANIM_OFF"
        otype = "roller"
        obj = await cg.get_variable(config[CONF_ROLLER])
    publish = f"{var}->publish_index(lv_{otype}_get_selected({obj}))"
    init.extend(
        set_event_cb(
            obj,
            publish,
            "LV_EVENT_VALUE_CHANGED",
        )
    )
    init.extend(
        [
            f"""{var}->set_options(lv_{otype}_get_options({obj}));
            {var}->set_control_lambda([] (size_t v) {{
                lv_{otype}_set_selected({obj}, v {animated});
               {publish};
            }})""",
            publish,
        ]
    )
    await add_init_lambda(paren, init)
