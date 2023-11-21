import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from . import add_init_lambda, lv_btn_t

CONF_BTN_ID = "btn_id"
CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(binary_sensor.BinarySensor).extend(
    {
        cv.Required(CONF_BTN_ID): cv.use_id(lv_btn_t),
    }
)


async def to_code(config):
    sensor = await binary_sensor.new_binary_sensor(config)
    obj = await cg.get_variable(config[CONF_BTN_ID])
    await add_init_lambda(
        [
            f"lv_obj_add_event_cb({obj}, [](lv_event_t *e)\n" " {\n",
            f"   {sensor}->publish_state(true);\n" "}, LV_EVENT_PRESSED, nullptr)",
            f"lv_obj_add_event_cb({obj}, [](lv_event_t *e)\n" " {\n",
            f"   {sensor}->publish_state(false);\n" "}, LV_EVENT_RELEASED, nullptr)",
        ]
    )
