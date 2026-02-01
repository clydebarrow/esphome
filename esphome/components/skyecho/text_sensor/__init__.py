import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.config_validation import polling_component_schema
from esphome.const import CONF_TYPE

from .. import CONF_SKYECHO_ID, SkyEchoComponent, skyecho_ns

DEPENDENCIES = ["skyecho"]

SkyEchoTextSensor = skyecho_ns.class_(
    "SkyEchoTextSensor", text_sensor.TextSensor, cg.Component
)

SkyEchoTrafficListSensor = skyecho_ns.class_(
    "SkyEchoTrafficListSensor", text_sensor.TextSensor, cg.PollingComponent
)

SkyEchoTextSensorType = skyecho_ns.enum("SkyEchoTextSensorType")
TEXT_SENSOR_TYPES = {
    "nmea": SkyEchoTextSensorType.NMEA,
    "traffic_list": SkyEchoTextSensorType.TRAFFIC_LIST,
}

CONFIG_SCHEMA = cv.typed_schema(
    {
        "nmea": text_sensor.text_sensor_schema(SkyEchoTextSensor)
        .extend(polling_component_schema("1s"))
        .extend(
            {
                cv.GenerateID(CONF_SKYECHO_ID): cv.use_id(SkyEchoComponent),
            }
        ),
        "traffic_list": text_sensor.text_sensor_schema(SkyEchoTrafficListSensor)
        .extend(polling_component_schema("1s"))
        .extend(
            {
                cv.GenerateID(CONF_SKYECHO_ID): cv.use_id(SkyEchoComponent),
            }
        ),
    },
    default_type="nmea",
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_SKYECHO_ID])
    cg.add(var.set_parent(parent))

    if config[CONF_TYPE] == "traffic_list":
        cg.add(parent.register_traffic_list_sensor(var))
    else:
        cg.add(parent.register_text_sensor(var))
