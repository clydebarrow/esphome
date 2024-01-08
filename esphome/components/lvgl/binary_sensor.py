import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.binary_sensor import (
    binary_sensor_schema,
    BinarySensor,
    new_binary_sensor,
)
from . import (
    add_init_lambda,
    LVGL_SCHEMA,
    CONF_LVGL_ID,
    CONF_OBJ,
    get_matrix_button,
    lv_pseudo_button_t,
    CONF_BTN,
    requires_component,
)

BASE_SCHEMA = binary_sensor_schema(BinarySensor).extend(LVGL_SCHEMA)
CONFIG_SCHEMA = cv.All(
    BASE_SCHEMA.extend(
        {
            cv.Required(CONF_OBJ): cv.use_id(lv_pseudo_button_t),
            cv.Required(CONF_OBJ): cv.use_id(lv_pseudo_button_t),
        }
    ),
    requires_component("binary_sensor"),
)


async def to_code(config):
    sensor = await new_binary_sensor(config)
    paren = await cg.get_variable(config[CONF_LVGL_ID])
    type, obj = await get_matrix_button(config[CONF_OBJ])
    if type == CONF_BTN:
        # map the button ID to the button matrix and an index
        idx = obj[1]
        obj = obj[0]
        test = f"if (lv_btnmatrix_get_selected_btn({obj}) == {idx})"
    else:
        test = ""
    await add_init_lambda(
        paren,
        [
            f"lv_obj_add_event_cb({obj}, [](lv_event_t *e) {{"
            f"{test} {sensor}->publish_state(true);"
            "}, LV_EVENT_PRESSING, nullptr)",
            f"lv_obj_add_event_cb({obj}, [](lv_event_t *e) {{"
            f"{test} {sensor}->publish_state(false);"
            "}, LV_EVENT_RELEASED, nullptr)",
        ],
    )
