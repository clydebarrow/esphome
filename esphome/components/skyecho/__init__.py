from esphome import automation
import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

DOMAIN = "skyecho"

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["network"]

# Key under CORE.data[DOMAIN] set when the LVGL traffic widget is configured.
KEY_NEEDS_IMAGES = "needs_images"


def AUTO_LOAD(config):
    # image/file are only needed when the LVGL traffic widget bundles its symbols.
    # image depends on display, so it must not be pulled in for display-less setups.
    loads = ["socket", "uart"]
    if CORE.data.get(DOMAIN, {}).get(KEY_NEEDS_IMAGES):
        loads += ["image", "file"]
    return loads


CONF_ALARM_ALTITUDE = "alarm_altitude"
CONF_ALARM_DISTANCE = "alarm_distance"
CONF_ALARM_TIME = "alarm_time"
CONF_FLARM_UART = "flarm_uart"
CONF_GLIDER_ALARM_ALTITUDE = "glider_alarm_altitude"
CONF_MAX_ALTITUDE_DIFFERENCE = "max_altitude_difference"
CONF_MAX_DISTANCE = "max_distance"
CONF_ON_THREAT_WARNING = "on_threat_warning"
CONF_WARNING_TIMES = "warning_times"

METERS_PER_NAUTICAL_MILE = 1852.0
METERS_PER_FOOT = 0.3048

skyecho_ns = cg.esphome_ns.namespace("skyecho")
SkyEchoComponent = skyecho_ns.class_("SkyEcho", cg.PollingComponent)

CONF_SKYECHO_ID = "skyecho_id"


def length(value):
    """A length in meters, also accepting nautical miles ("nm") and feet ("ft")."""
    if isinstance(value, str):
        text = value.strip()
        for suffix, meters in (
            ("nm", METERS_PER_NAUTICAL_MILE),
            ("ft", METERS_PER_FOOT),
        ):
            if text.lower().endswith(suffix):
                return cv.positive_float(text[: -len(suffix)].strip()) * meters
    return cv.distance(value)


def positive_length(value):
    return cv.float_range(min_included=False, min=0.0)(length(value))


def validate_warning_times(config):
    alarm_time = config[CONF_ALARM_TIME].total_seconds
    if any(t.total_seconds > alarm_time for t in config[CONF_WARNING_TIMES]):
        raise cv.Invalid(
            f"'{CONF_WARNING_TIMES}' must not be longer than '{CONF_ALARM_TIME}'",
            path=[CONF_WARNING_TIMES],
        )
    return config


def validate_uart_optional(config):
    """Only validate UART if it's specified."""
    if CONF_FLARM_UART in config:
        return uart.final_validate_device_schema(
            "skyecho", uart_bus=CONF_FLARM_UART, require_rx=True
        )(config)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SkyEchoComponent),
            cv.Optional(CONF_FLARM_UART): cv.use_id(uart.UARTComponent),
            # Collision prediction: close proximity, and how long before reaching it the alarm is raised.
            cv.Optional(CONF_ALARM_DISTANCE, default="0.5nm"): positive_length,
            cv.Optional(CONF_ALARM_ALTITUDE, default="500ft"): positive_length,
            # Used instead of alarm_altitude when both the ownship and the target are gliders.
            cv.Optional(CONF_GLIDER_ALARM_ALTITUDE, default="200ft"): positive_length,
            cv.Optional(
                CONF_ALARM_TIME, default="30s"
            ): cv.positive_time_period_seconds,
            # Targets further away than these are ignored, unless they raise the alarm.
            cv.Optional(CONF_MAX_DISTANCE, default="10nm"): positive_length,
            cv.Optional(
                CONF_MAX_ALTITUDE_DIFFERENCE, default="3000ft"
            ): positive_length,
            # Times before close proximity at which on_threat_warning is called.
            cv.Optional(
                CONF_WARNING_TIMES, default=["30s", "20s", "10s", "5s"]
            ): cv.ensure_list(cv.positive_time_period_seconds),
            cv.Optional(CONF_ON_THREAT_WARNING): automation.validate_automation({}),
        }
    ).extend(cv.polling_component_schema("1s")),
    validate_warning_times,
)

FINAL_VALIDATE_SCHEMA = validate_uart_optional


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if flarm_uart_config := config.get(CONF_FLARM_UART):
        uart_component = await cg.get_variable(flarm_uart_config)
        cg.add(var.set_flarm_uart(uart_component))

    cg.add(var.set_alarm_distance(config[CONF_ALARM_DISTANCE]))
    cg.add(var.set_alarm_altitude(config[CONF_ALARM_ALTITUDE]))
    cg.add(var.set_glider_alarm_altitude(config[CONF_GLIDER_ALARM_ALTITUDE]))
    cg.add(var.set_alarm_time(config[CONF_ALARM_TIME].total_seconds))
    cg.add(var.set_max_distance(config[CONF_MAX_DISTANCE]))
    cg.add(var.set_max_altitude_difference(config[CONF_MAX_ALTITUDE_DIFFERENCE]))
    warning_times = sorted(
        {t.total_seconds for t in config[CONF_WARNING_TIMES]}, reverse=True
    )
    cg.add(var.set_warning_times(warning_times))
    for conf in config.get(CONF_ON_THREAT_WARNING, []):
        await automation.build_callback_automation(
            var, "add_on_threat_warning_callback", [(cg.uint32, "seconds")], conf
        )


# Registers the custom "skyecho_traffic" LVGL widget. Importing here (after the
# definitions above) ensures the widget type is available whenever the skyecho
# component is loaded, before LVGL builds its widget schema.
from . import traffic_widget  # noqa: E402,F401  pylint: disable=wrong-import-position
