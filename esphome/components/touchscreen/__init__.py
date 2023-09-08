import esphome.config_validation as cv
import esphome.codegen as cg

from esphome.components import display
from esphome import automation
from esphome.const import CONF_ON_TOUCH, CONF_DIMENSIONS, CONF_ROTATION
from esphome.core import coroutine_with_priority

CODEOWNERS = ["@jesserockz"]
DEPENDENCIES = ["display"]

IS_PLATFORM_COMPONENT = True

touchscreen_ns = cg.esphome_ns.namespace("touchscreen")

Touchscreen = touchscreen_ns.class_("Touchscreen", cg.PollingComponent)
TouchRotation = touchscreen_ns.enum("TouchRotation")
TouchPoint = touchscreen_ns.struct("TouchPoint")
TouchListener = touchscreen_ns.class_("TouchListener")

CONF_DISPLAY = "display"
CONF_TOUCHSCREEN_ID = "touchscreen_id"
# not used yet: CONF_REPORT_INTERVAL = "report_interval"
CONF_SWAP_X_Y = "swap_x_y"

DISPLAY_ROTATIONS = {
    0: touchscreen_ns.ROTATE_0_DEGREES,
    90: touchscreen_ns.ROTATE_90_DEGREES,
    180: touchscreen_ns.ROTATE_180_DEGREES,
    270: touchscreen_ns.ROTATE_2700_DEGREES,
}


def validate_rotation(value):
    value = cv.string(value)
    if value.endswith("°"):
        value = value[:-1]
    return cv.enum(DISPLAY_ROTATIONS, int=True)(value)


def report_interval(value):
    if value == "never":
        return 4294967295  # uint32_t max
    return cv.positive_time_period_milliseconds(value)


def validate_hasdisplay(config):
    if CONF_DISPLAY in config:
        if CONF_DIMENSIONS in config:
            raise cv.Invalid(
                CONF_DIMENSIONS
                + " is not useful when the touchscreen is connected to a display "
            )
        if CONF_ROTATION in config:
            raise cv.Invalid(
                CONF_ROTATION
                + " is not useful when the touchscreen is connected to a display "
            )
    else:
        if CONF_DIMENSIONS not in config:
            raise cv.Invalid(
                CONF_DIMENSIONS
                + " is mandatory when this touchscreen is NOT connected to a display "
            )


TOUCHSCREEN_SCHEMA = (
    cv.Schema(
        {
            cv.Optional(CONF_DISPLAY): cv.use_id(display.DisplayBuffer),
            cv.Optional(CONF_DIMENSIONS): cv.dimensions,
            cv.Optional(CONF_ROTATION): validate_rotation,
            cv.Optional(CONF_SWAP_X_Y, default=False): cv.boolean,
            cv.Optional(CONF_ON_TOUCH): automation.validate_automation(single=True),
            # not used yet: cv.Optional(CONF_REPORT_INTERVAL, default="never"): report_interval,
        }
    )
    .extend(cv.polling_component_schema("50ms"))
    .add_extra(validate_hasdisplay)
)


async def register_touchscreen(var, config):
    await cg.register_component(var, config)

    if CONF_DISPLAY in config:
        disp = await cg.get_variable(config[CONF_DISPLAY])
        cg.add(var.set_display(disp))
    else:
        if CONF_DIMENSIONS in config:
            cg.add(
                var.set_display_dimension(
                    config[CONF_DIMENSIONS][0], config[CONF_DIMENSIONS][1]
                )
            )
        if CONF_ROTATION in config:
            cg.add(var.set_rotation(config[CONF_ROTATION]))
    cg.add(var.set_swap_x_y(config[CONF_SWAP_X_Y]))
    # not used yet: cg.add(var.set_report_interval(config[CONF_REPORT_INTERVAL]))

    if CONF_ON_TOUCH in config:
        await automation.build_automation(
            var.get_touch_trigger(),
            [(TouchPoint, "touch")],
            config[CONF_ON_TOUCH],
        )


@coroutine_with_priority(100.0)
async def to_code(config):
    cg.add_global(touchscreen_ns.using)
    cg.add_define("USE_TOUCHSCREEN")
