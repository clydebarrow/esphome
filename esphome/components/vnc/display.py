import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display
from . import VNCDisplay
from esphome.const import (
    CONF_DIMENSIONS,
    CONF_WIDTH,
    CONF_HEIGHT,
    CONF_LAMBDA,
    CONF_PAGES,
    CONF_ID,
    CONF_PORT,
)

CONFIG_SCHEMA = cv.All(
    display.FULL_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(VNCDisplay),
            cv.Optional(CONF_PORT, default=5900): cv.positive_int,
            cv.Required(CONF_DIMENSIONS): cv.Any(
                cv.dimensions,
                cv.Schema(
                    {
                        cv.Required(CONF_WIDTH): cv.int_,
                        cv.Required(CONF_HEIGHT): cv.int_,
                    }
                ),
            ),
        }
    ).extend(cv.polling_component_schema("1s")),
    cv.has_at_most_one_key(CONF_PAGES, CONF_LAMBDA),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add_define("CONFIG_HTTPD_WS_SUPPORT", 1)
    await display.register_display(var, config)
    cg.add(var.set_port(config[CONF_PORT]))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(display.DisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))

    dimensions = config[CONF_DIMENSIONS]
    if isinstance(dimensions, dict):
        cg.add(var.set_dimensions(dimensions[CONF_WIDTH], dimensions[CONF_HEIGHT]))
    else:
        (width, height) = dimensions
        cg.add(var.set_dimensions(width, height))
