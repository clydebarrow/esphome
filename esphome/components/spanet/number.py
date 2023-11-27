import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from . import Spanet, spanet_ns
from esphome.const import (
    CONF_ID,
    CONF_TARGET_TEMPERATURE,
)

SpanetNumber = spanet_ns.class_("SpanetNumber", number.Number)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(Spanet),
        cv.Optional(CONF_TARGET_TEMPERATURE): number.number_schema(SpanetNumber),
    }
)


async def to_code(config):
    var = await cg.get_variable(config[CONF_ID])
    if CONF_TARGET_TEMPERATURE in config:
        target = await number.new_number(
            config[CONF_TARGET_TEMPERATURE], min_value=10.0, max_value=41.0, step=0.1
        )
        await cg.register_parented(target, var)
        cg.add(var.set_target_temperature(target))
        cg.add(target.set_command("W40"))
        cg.add(target.set_scale(0.1))
