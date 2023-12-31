import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.binary_sensor import (
    binary_sensor_schema,
    BinarySensor,
    new_binary_sensor,
)
from esphome.const import CONF_EVENT
from .. import (
    add_init_lambda,
    LVGL_SCHEMA,
    CONF_LVGL_ID,
    lv_one_of,
    CONF_BTN,
    CONF_CHECKBOX,
    lv_btn_t,
    lv_checkbox_t,
)

EVENTS = (
    "PRESSED",
    "SHORT_CLICKED",
    "LONG_PRESSED",
    "CLICKED",
    "VALUE_CHANGED",
)

BASE_SCHEMA = binary_sensor_schema(BinarySensor).extend(LVGL_SCHEMA)
CONFIG_SCHEMA = cv.Any(
    BASE_SCHEMA.extend(
        {
            cv.Required(CONF_BTN): cv.use_id(lv_btn_t),
            cv.Optional(CONF_EVENT, default="PRESSED"): lv_one_of(EVENTS, "LV_EVENT_"),
        }
    ),
    BASE_SCHEMA.extend(
        {
            cv.Required(CONF_CHECKBOX): cv.use_id(lv_checkbox_t),
            cv.Optional(CONF_EVENT, default="VALUE_CHANGED"): lv_one_of(
                EVENTS, "LV_EVENT_"
            ),
        }
    ),
)


async def to_code(config):
    sensor = await new_binary_sensor(config)
    if CONF_BTN in config:
        id = config[CONF_BTN]
    else:
        id = config[CONF_CHECKBOX]
    obj = await cg.get_variable(id)
    event = config[CONF_EVENT]
    paren = await cg.get_variable(config[CONF_LVGL_ID])
    init = []
    action = f"   {sensor}->publish_state(true);\n"
    if event == "LV_EVENT_VALUE_CHANGED":
        action = f"   {sensor}->publish_state(((int)lv_obj_get_state({obj}) & (int)LV_STATE_CHECKED) != 0);\n"
    else:
        # click actions are not fired until after release
        if "CLICKED" in event:
            action = action + f"   {sensor}->publish_state(false);\n"
        else:
            init = [
                f"lv_obj_add_event_cb({obj}, [](lv_event_t *e)\n"
                " {\n"
                f"   {sensor}->publish_state(false);\n"
                "}, LV_EVENT_RELEASED, nullptr)",
            ]
    await add_init_lambda(
        paren,
        init
        + [
            f"lv_obj_add_event_cb({obj}, [](lv_event_t *e)\n"
            " {\n" + f'ESP_LOGD("event", "state = %X", lv_obj_get_state({obj}))',
            action,
            f"}}, {event}, nullptr)",
        ],
    )
