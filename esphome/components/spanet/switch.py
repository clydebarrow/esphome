import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import (
    CONF_ID,
)
from . import Spanet, spanet_ns

SpanetSwitch = spanet_ns.class_("SpanetSwitch", switch.Switch)

CONF_ROW = "row"
CONF_COL = "col"
CONF_CMD = "cmd"


def sw_desc(row, col, cmd):
    return {
        CONF_ROW: row,
        CONF_COL: col,
        CONF_CMD: cmd,
    }


SWITCHES = (
    dict(map(lambda n: (f"pump{n}", sw_desc(5, n + 18, f"S{21 + n}")), [1, 2, 3, 4, 5]))
    | {}
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(Spanet),
    }
).extend(
    cv.Schema(
        dict(
            map(
                lambda s: (cv.Optional(s), switch.switch_schema(SpanetSwitch)), SWITCHES
            )
        )
    )
)


async def to_code(config):
    var = await cg.get_variable(config[CONF_ID])
    for s in SWITCHES:
        if s in config:
            sw_data = SWITCHES[s]
            target = await switch.new_switch(
                s, [CONF_ROW], sw_data[CONF_COL], sw_data[CONF_CMD]
            )
            await cg.register_parented(target, var)
            cg.add(var.add_switch(target))
