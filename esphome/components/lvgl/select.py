import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import (
    CONF_ID,
)
from . import (
    lvgl_ns,
    LVGL_SCHEMA,
    CONF_LVGL_ID,
    add_init_lambda,
    set_event_cb,
    CONF_DROPDOWN,
    lv_dropdown_t,
)

LVGLSelect = lvgl_ns.class_("LVGLSelect", select.Select)

CONFIG_SCHEMA = (
    select.select_schema(LVGLSelect)
    .extend(LVGL_SCHEMA)
    .extend(
        {
            cv.Required(CONF_DROPDOWN): cv.use_id(lv_dropdown_t),
        }
    )
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await select.register_select(var, config, options=[])
    paren = await cg.get_variable(config[CONF_LVGL_ID])
    drop = config[CONF_DROPDOWN]
    obj = await cg.get_variable(drop)
    publish = f"{var}->publish_index(lv_dropdown_get_selected({obj}))"
    init = set_event_cb(
        obj,
        publish,
        "LV_EVENT_VALUE_CHANGED",
    )
    init.extend(
        [
            f"""{var}->set_options(lv_dropdown_get_options({obj}));
            {var}->set_control_lambda([] (size_t v) {{
                lv_dropdown_set_selected({obj}, v);
               {publish};
            }})""",
            publish,
        ]
    )
    await add_init_lambda(paren, init)
