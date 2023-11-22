import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.sensor import sensor_schema, new_sensor, Sensor
from . import add_init_lambda, lv_arc_t, LVGL_SCHEMA, CONF_LVGL_ID, lv_slider_t
from ...const import CONF_VALUE

CONF_ARC_ID = "arc_id"
CONF_SLIDER_ID = "slider_id"
CONFIG_SCHEMA = (
    sensor_schema(Sensor)
    .extend(LVGL_SCHEMA)
    .extend(
        {
            cv.Exclusive(CONF_ARC_ID, CONF_VALUE): cv.use_id(lv_arc_t),
            cv.Exclusive(CONF_SLIDER_ID, CONF_VALUE): cv.use_id(lv_slider_t),
        }
    )
)


async def to_code(config):
    sensor = await new_sensor(config)
    paren = await cg.get_variable(config[CONF_LVGL_ID])
    if CONF_ARC_ID in config:
        obj = await cg.get_variable(config[CONF_ARC_ID])
        lv_type = "arc"
    elif CONF_SLIDER_ID in config:
        obj = await cg.get_variable(config[CONF_SLIDER_ID])
        lv_type = "slider"
    else:
        return
    await add_init_lambda(
        paren,
        [
            f"lv_obj_add_event_cb({obj}, [](lv_event_t *e)\n"
            " {\n"
            f"   {sensor}->publish_state(lv_{lv_type}_get_value({obj}));\n"
            "}, LV_EVENT_VALUE_CHANGED, nullptr)",
        ],
    )
