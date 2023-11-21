import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from . import lv_obj_t, CONF_OBJ_ID, add_init_lambda

CONFIG_SCHEMA = (
    binary_sensor.binary_sensor_schema(binary_sensor.BinarySensor)
    .extend(
        {
            cv.Required(CONF_OBJ_ID): cv.use_id(lv_obj_t),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    sensor = await binary_sensor.new_binary_sensor(config)
    obj = await cg.get_variable(config[CONF_OBJ_ID])
    await add_init_lambda(
        [
            f"lv_obj_add_event_cb({obj}, [](lv_event_t *e)\n" " {\n",
            '    esph_log_d("lvgl.binary_sensor", "event %X", e->code);\n'
            f"   if (e->code == LV_EVENT_PRESSED) {sensor}->publish_state(true);\n"
            f"   if (e->code == LV_EVENT_RELEASED) {sensor}->publish_state(false);\n"
            "}, (lv_event_code_t)((int)LV_EVENT_PRESSED|(int)LV_EVENT_RELEASED), nullptr)",
        ]
    )
