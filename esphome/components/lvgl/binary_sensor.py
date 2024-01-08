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
    lv_obj_t,
    LvBtnmBtn,
    get_matrix_button,
)

CONF_MATRIX_BTN = "matrix_btn"

BASE_SCHEMA = binary_sensor_schema(BinarySensor).extend(LVGL_SCHEMA)
CONFIG_SCHEMA = cv.All(
    BASE_SCHEMA.extend(
        {
            cv.Exclusive(CONF_OBJ, "object"): cv.use_id(lv_obj_t),
            cv.Exclusive(CONF_MATRIX_BTN, "object"): cv.use_id(LvBtnmBtn),
        }
    ),
    cv.has_at_least_one_key(CONF_OBJ, CONF_MATRIX_BTN),
)


async def to_code(config):
    sensor = await new_binary_sensor(config)
    paren = await cg.get_variable(config[CONF_LVGL_ID])
    init = []
    if id := config.get(CONF_OBJ):
        obj = await cg.get_variable(id)
        test = ""
    elif id := config.get(CONF_MATRIX_BTN):
        (obj, idx) = await get_matrix_button(id)
        test = f"if (lv_btnmatrix_get_selected_btn({obj}) == {idx})"
    else:
        return
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
