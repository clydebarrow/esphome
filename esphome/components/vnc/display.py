from esphome import automation
import esphome.codegen as cg
from esphome.components import display
import esphome.config_validation as cv
from esphome.const import (
    CONF_DIMENSIONS,
    CONF_HEIGHT,
    CONF_ID,
    CONF_LAMBDA,
    CONF_ON_CONNECT,
    CONF_ON_DISCONNECT,
    CONF_PAGES,
    CONF_PASSWORD,
    CONF_PORT,
    CONF_TRIGGER_ID,
    CONF_USERNAME,
    CONF_WIDTH,
)
from esphome.core import Lambda

from . import VNCDisplay, VNCTrigger

DEPENDENCIES = ["network"]
AUTO_LOAD = ["socket", "touchscreen"]

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
            cv.Optional(CONF_ON_CONNECT): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(VNCTrigger),
                }
            ),
            cv.Optional(CONF_ON_DISCONNECT): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(VNCTrigger),
                }
            ),
            cv.Optional(CONF_USERNAME, default="esphome"): cv.string,
            cv.Optional(CONF_PASSWORD): cv.string,
        }
    ).extend(cv.polling_component_schema("1s")),
    cv.has_at_most_one_key(CONF_PAGES, CONF_LAMBDA),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add_define("CONFIG_HTTPD_WS_SUPPORT", 1)
    await display.register_display(var, config)
    cg.add(var.set_port(config[CONF_PORT]))

    cg.add(var.set_username(config[CONF_USERNAME]))
    if password := config.get(CONF_PASSWORD):
        cg.add(var.set_password(password))

    if lambconf := config.get(CONF_LAMBDA):
        lambda_ = await cg.process_lambda(
            lambconf, [(display.DisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))

    if conf := config.get(CONF_ON_CONNECT):
        conf = conf[0]
        trigger = cg.new_Pvariable(
            conf[CONF_TRIGGER_ID],
        )
        await automation.build_automation(trigger, [], conf)
        lamb = await cg.process_lambda(
            Lambda(f"{trigger}->trigger();"),
            [],
            return_type=cg.void,
        )
        cg.add(var.set_on_connect(lamb))

    if conf := config.get(CONF_ON_DISCONNECT):
        conf = conf[0]
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
        lamb = await cg.process_lambda(
            Lambda(f"{trigger}->trigger();"),
            [(display.DisplayRef, "it")],
            return_type=cg.void,
        )
        cg.add(var.set_on_disconnect(lamb))

    dimensions = config[CONF_DIMENSIONS]
    if isinstance(dimensions, dict):
        (width, height) = dimensions[CONF_WIDTH], dimensions[CONF_HEIGHT]
    else:
        (width, height) = dimensions
    cg.add(var.set_dimensions(width, height))
