import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_ID,
    CONF_TARGET_TEMPERATURE,
)
from . import Spanet, spanet_ns, CONF_ROW, CONF_COL, CONF_CMD, CONF_SCALE

SpanetNumber = spanet_ns.class_("SpanetNumber", number.Number)


def num_desc(row, col, cmd, scale):
    return {CONF_ROW: ord(row[0]), CONF_COL: col, CONF_CMD: cmd, CONF_SCALE: scale}


NUMBERS = {
    "target_temperature": num_desc("6", 9, "W40", 0.1),
}
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(Spanet),
    }
).extend(
    cv.Schema(
        dict(
            map(lambda s: (cv.Optional(s), number.number_schema(SpanetNumber)), NUMBERS)
        )
    )
)


async def to_code(config):
    var = await cg.get_variable(config[CONF_ID])
    for s in NUMBERS:
        if s in config:
            num_data = NUMBERS[s]
            numvar = await number.new_number(
                config[s],
                num_data[CONF_ROW],
                num_data[CONF_COL],
                num_data[CONF_CMD],
                num_data[CONF_SCALE],
                min_value=10.0,
                max_value=41.0,
                step=0.1,
            )
            await cg.register_parented(numvar, var)
            cg.add(var.add_value(numvar))
