import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.binary_sensor import (
    binary_sensor_schema,
    BinarySensor,
    new_binary_sensor,
)
from . import add_init_lambda, lv_btn_t, LVGL_SCHEMA, CONF_LVGL_ID

CONF_BTN_ID = "btn_id"
CONFIG_SCHEMA = (
    binary_sensor_schema(BinarySensor)
    .extend(LVGL_SCHEMA)
    .extend(
        {
            cv.Required(CONF_BTN_ID): cv.use_id(lv_btn_t),
        }
    )
)


async def to_code(config):
    sensor = await new_binary_sensor(config)
    obj = await cg.get_variable(config[CONF_BTN_ID])
    paren = await cg.get_variable(config[CONF_LVGL_ID])
    await add_init_lambda(
        paren,
        [
            f"lv_obj_add_event_cb({obj}, [](lv_event_t *e)\n" " {\n",
            f"   {sensor}->publish_state(true);\n" "}, LV_EVENT_PRESSED, nullptr)",
            f"lv_obj_add_event_cb({obj}, [](lv_event_t *e)\n" " {\n",
            f"   {sensor}->publish_state(false);\n" "}, LV_EVENT_RELEASED, nullptr)",
        ],
    )
