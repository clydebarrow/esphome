import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.sensor import sensor_schema, new_sensor, Sensor
from esphome.const import CONF_VALUE
from . import (
    add_init_lambda,
    lv_arc_t,
    LVGL_SCHEMA,
    CONF_LVGL_ID,
    lv_slider_t,
    set_event_cb,
    CONF_SLIDER,
    CONF_ARC,
)

CONF_ARC_ID = "arc_id"
CONF_SLIDER_ID = "slider_id"
CONFIG_SCHEMA = (
    sensor_schema(Sensor)
    .extend(LVGL_SCHEMA)
    .extend(
        {
            cv.Exclusive(CONF_ARC, CONF_VALUE): cv.use_id(lv_arc_t),
            cv.Exclusive(CONF_SLIDER, CONF_VALUE): cv.use_id(lv_slider_t),
        }
    )
)


async def to_code(config):
    sensor = await new_sensor(config)
    paren = await cg.get_variable(config[CONF_LVGL_ID])
    if arc := config.get(CONF_ARC):
        obj = await cg.get_variable(arc)
        lv_type = "arc"
    elif slider := config.get(CONF_SLIDER):
        obj = await cg.get_variable(slider)
        lv_type = "slider"
    else:
        return
    await add_init_lambda(
        paren,
        set_event_cb(
            obj,
            f"   {sensor}->publish_state(lv_{lv_type}_get_value({obj}));\n",
            "LV_EVENT_VALUE_CHANGED",
            f"{paren}->get_custom_change_event()",
        ),
    )
