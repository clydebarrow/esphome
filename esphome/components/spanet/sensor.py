import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_TEMPERATURE,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)
from . import Spanet, spanet_ns, CONF_ROW, CONF_COL, CONF_CMD, CONF_SCALE

SpanetSensor = spanet_ns.class_("SpanetSensor", sensor.Sensor)


def num_desc(row, col, scale):
    return {CONF_ROW: ord(row[0]), CONF_COL: col, CONF_SCALE: scale}


TEMPERATURES = {
    "temperature": num_desc("5", 16, 0.1),
}
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(Spanet),
    }
).extend(
    cv.Schema(
        dict(
            map(
                lambda s: (
                    cv.Optional(s),
                    sensor.sensor_schema(
                        SpanetSensor,
                        unit_of_measurement=UNIT_CELSIUS,
                        accuracy_decimals=1,
                        device_class=DEVICE_CLASS_TEMPERATURE,
                        state_class=STATE_CLASS_MEASUREMENT,
                    ),
                ),
                TEMPERATURES,
            )
        )
    )
)


async def to_code(config):
    var = await cg.get_variable(config[CONF_ID])
    for s in TEMPERATURES:
        if s in config:
            sens_data = TEMPERATURES[s]
            sensvar = await sensor.new_sensor(
                config[s],
                sens_data[CONF_ROW],
                sens_data[CONF_COL],
                sens_data[CONF_SCALE],
            )
            await cg.register_parented(sensvar, var)
            cg.add(var.add_value(sensvar))
