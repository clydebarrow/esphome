import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.sensor import sensor_schema, new_sensor, Sensor
from . import add_init_lambda, lv_arc_t
from ...const import CONF_ID

CONF_ARC_ID = "arc_id"
CONFIG_SCHEMA = (
    sensor_schema(Sensor)
    .extend(
        {
            cv.Required(CONF_ARC_ID): cv.use_id(lv_arc_t),
        }
    )
)


async def to_code(config):
    sensor = await new_sensor(config)
    obj = await cg.get_variable(config[CONF_ARC_ID])
    await add_init_lambda(
        [
            f"lv_obj_add_event_cb({obj}, [](lv_event_t *e)\n" " {\n",
            f"   {sensor}->publish_state(lv_arc_get_value({obj}));\n"
            "}, (lv_event_code_t)LV_EVENT_VALUE_CHANGED, nullptr)",
        ]
    )
