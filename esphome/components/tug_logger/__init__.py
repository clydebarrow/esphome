from esphome import automation
import esphome.codegen as cg
from esphome.components import (
    binary_sensor,
    esp32,
    runtime_image,
    sensor,
    time as time_,
)
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIME_ID, PLATFORM_ESP32, PLATFORM_HOST
from esphome.core import CORE
from esphome.types import ConfigType

CODEOWNERS = ["@clydebarrow"]
AUTO_LOAD = ["binary_sensor", "json", "runtime_image", "sensor"]
DEPENDENCIES = ["network", "time"]
DOMAIN = "tug_logger"

CONF_BATTERY_ID = "battery_id"
CONF_CARD_PATH = "card_path"
CONF_EXTERNAL_POWER_ID = "external_power_id"
CONF_GPS_ID = "gps_id"
CONF_LANDING_CONFIRM = "landing_confirm"
CONF_LANDING_SPEED = "landing_speed"
CONF_ON_LANDED = "on_landed"
CONF_ON_POWER_OFF = "on_power_off"
CONF_ON_TAKEOFF = "on_takeoff"
CONF_SHUTDOWN_WINDOW = "shutdown_window"
CONF_SKYECHO_ID = "skyecho_id"
CONF_TAKEOFF_CONFIRM = "takeoff_confirm"
CONF_TAKEOFF_SPEED = "takeoff_speed"
CONF_UPLOAD_KEY = "upload_key"
CONF_UPLOAD_URL = "upload_url"
CONF_WIFI_NETWORKS_ID = "wifi_networks_id"

KNOTS_TO_MPS = 1852.0 / 3600.0

tug_logger_ns = cg.esphome_ns.namespace(DOMAIN)
TugLogger = tug_logger_ns.class_("TugLogger", cg.Component)
# Named here rather than imported, so these components are only loaded by configs that use them
SkyEcho = cg.esphome_ns.namespace("skyecho").class_("SkyEcho", cg.PollingComponent)
GPS = cg.esphome_ns.namespace("gps").class_("GPS", cg.PollingComponent)
WifiNetworks = cg.esphome_ns.namespace("wifi_networks").class_(
    "WifiNetworks", cg.Component
)


async def gps_listener_registration(hub: cg.MockObj, var: cg.MockObj) -> None:
    # Imported here, so the gps component is only loaded by configs that use it
    from esphome.components.gps import register_listener  # pylint: disable=import-outside-toplevel

    await register_listener(hub, var)


def knots(value: object) -> float:
    """A speed in knots, as a number or with the suffix "kt", converted to m/s."""
    if isinstance(value, str) and value.strip().lower().endswith("kt"):
        value = value.strip()[:-2]
    return cv.float_range(min_included=False, min=0.0)(cv.float_(value)) * KNOTS_TO_MPS


def _validate_speeds(config: ConfigType) -> ConfigType:
    if config[CONF_LANDING_SPEED] >= config[CONF_TAKEOFF_SPEED]:
        raise cv.Invalid(
            f"'{CONF_LANDING_SPEED}' must be below '{CONF_TAKEOFF_SPEED}'",
            path=[CONF_LANDING_SPEED],
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TugLogger),
            cv.GenerateID(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
            cv.Optional(CONF_SKYECHO_ID): cv.use_id(SkyEcho),
            cv.Optional(CONF_GPS_ID): cv.use_id(GPS),
            cv.Optional(CONF_WIFI_NETWORKS_ID): cv.use_id(WifiNetworks),
            cv.Optional(CONF_EXTERNAL_POWER_ID): cv.use_id(binary_sensor.BinarySensor),
            cv.Optional(CONF_BATTERY_ID): cv.use_id(sensor.Sensor),
            # The mount point of the SD card in the C library (vfs_path of the filesystem)
            cv.Optional(CONF_CARD_PATH, default="/sdcard"): cv.string_strict,
            cv.Optional(CONF_TAKEOFF_SPEED, default="40kt"): knots,
            cv.Optional(
                CONF_TAKEOFF_CONFIRM, default="5s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_LANDING_SPEED, default="15kt"): knots,
            cv.Optional(
                CONF_LANDING_CONFIRM, default="30s"
            ): cv.positive_time_period_milliseconds,
            # How long the device stays up on battery when idle, to upload what is queued
            cv.Optional(
                CONF_SHUTDOWN_WINDOW, default="10min"
            ): cv.positive_time_period_milliseconds,
            # For testing against another server: used instead of the values in the tug's file on the card
            cv.Optional(CONF_UPLOAD_URL): cv.string_strict,
            cv.Optional(CONF_UPLOAD_KEY): cv.string_strict,
            cv.Optional(CONF_ON_TAKEOFF): automation.validate_automation({}),
            cv.Optional(CONF_ON_LANDED): automation.validate_automation({}),
            cv.Optional(CONF_ON_POWER_OFF): automation.validate_automation({}),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    # A host build shows the screens on the computer, with no uploads or firmware updates
    cv.only_on([PLATFORM_ESP32, PLATFORM_HOST]),
    _validate_speeds,
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_CARD_PATH])
    await cg.register_component(var, config)
    if CORE.is_esp32:
        esp32.request_http_client()
        esp32.require_certificate_bundle()
    # The splash screen's logo
    runtime_image.enable_format("PNG")

    cg.add(var.set_time(await cg.get_variable(config[CONF_TIME_ID])))
    if (skyecho := config.get(CONF_SKYECHO_ID)) is not None:
        cg.add_define("USE_TUG_LOGGER_SKYECHO")
        cg.add(var.set_skyecho(await cg.get_variable(skyecho)))
    if (gps_id := config.get(CONF_GPS_ID)) is not None:
        cg.add_define("USE_TUG_LOGGER_GPS")
        hub = await cg.get_variable(gps_id)
        await gps_listener_registration(hub, var)
    if (networks := config.get(CONF_WIFI_NETWORKS_ID)) is not None:
        cg.add_define("USE_TUG_LOGGER_WIFI_NETWORKS")
        cg.add(var.set_wifi_networks(await cg.get_variable(networks)))
    if (power := config.get(CONF_EXTERNAL_POWER_ID)) is not None:
        cg.add(var.set_external_power(await cg.get_variable(power)))
    if (battery := config.get(CONF_BATTERY_ID)) is not None:
        cg.add(var.set_battery(await cg.get_variable(battery)))

    cg.add(var.set_takeoff_speed(config[CONF_TAKEOFF_SPEED]))
    cg.add(var.set_takeoff_confirm(config[CONF_TAKEOFF_CONFIRM]))
    cg.add(var.set_landing_speed(config[CONF_LANDING_SPEED]))
    cg.add(var.set_landing_confirm(config[CONF_LANDING_CONFIRM]))
    cg.add(var.set_shutdown_window(config[CONF_SHUTDOWN_WINDOW]))
    if (url := config.get(CONF_UPLOAD_URL)) is not None:
        cg.add(var.set_upload_url(url))
    if (upload_key := config.get(CONF_UPLOAD_KEY)) is not None:
        cg.add(var.set_upload_key(upload_key))
    for key, method in (
        (CONF_ON_TAKEOFF, "add_on_takeoff_callback"),
        (CONF_ON_LANDED, "add_on_landed_callback"),
        (CONF_ON_POWER_OFF, "add_on_power_off_callback"),
    ):
        for conf in config.get(key, []):
            await automation.build_callback_automation(var, method, [], conf)
