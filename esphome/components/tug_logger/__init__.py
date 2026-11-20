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
CONF_ON_FIRMWARE_CHECK = "on_firmware_check"
CONF_ON_LANDED = "on_landed"
CONF_ON_POWER_OFF = "on_power_off"
CONF_ON_TAKEOFF = "on_takeoff"
CONF_SHUTDOWN_WINDOW = "shutdown_window"
CONF_SKYECHO_ID = "skyecho_id"
CONF_TAKEOFF_CONFIRM = "takeoff_confirm"
CONF_TAKEOFF_SPEED = "takeoff_speed"
CONF_UPLOAD_KEY = "upload_key"
CONF_UPLOAD_LINK_ID = "upload_link_id"
CONF_POSITION_LINK_ID = "position_link_id"
CONF_LORA_ADDRESSING_ID = "lora_addressing_id"
CONF_LORA_ENCRYPTION_ID = "lora_encryption_id"
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
packet_interface_ns = cg.esphome_ns.namespace("packet_interface")
PacketInterface = packet_interface_ns.class_("PacketInterface")
BasicAddressing = packet_interface_ns.class_("BasicAddressing", cg.Component)
XxteaEncryption = packet_interface_ns.class_("XxteaEncryption", PacketInterface)


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
            cv.Optional(CONF_LANDING_SPEED, default="30kt"): knots,
            cv.Optional(
                CONF_LANDING_CONFIRM, default="10s"
            ): cv.positive_time_period_milliseconds,
            # How long the device stays up on battery when idle, to upload what is queued
            cv.Optional(
                CONF_SHUTDOWN_WINDOW, default="10min"
            ): cv.positive_time_period_milliseconds,
            # For testing against another server: used instead of the values in the tug's file on the card
            cv.Optional(CONF_UPLOAD_URL): cv.string_strict,
            cv.Optional(CONF_UPLOAD_KEY): cv.string_strict,
            # Tows go over this packet interface, to a receiver that posts them, while there is no internet. Its stack
            # needs a fragmentation layer: see tow_link.h
            cv.Optional(CONF_UPLOAD_LINK_ID): cv.use_id(PacketInterface),
            # The tug's position is broadcast over this packet interface for the live map, as a glider tracker's is
            cv.Optional(CONF_POSITION_LINK_ID): cv.use_id(PacketInterface),
            # The upload link's addressing and its key_at_runtime encryption, given the tug's LoRa address and key
            # from the tug file
            cv.Inclusive(CONF_LORA_ADDRESSING_ID, "lora_layers"): cv.use_id(
                BasicAddressing
            ),
            cv.Inclusive(CONF_LORA_ENCRYPTION_ID, "lora_layers"): cv.use_id(
                XxteaEncryption
            ),
            cv.Optional(CONF_ON_TAKEOFF): automation.validate_automation({}),
            cv.Optional(CONF_ON_LANDED): automation.validate_automation({}),
            cv.Optional(CONF_ON_POWER_OFF): automation.validate_automation({}),
            # When a firmware check is asked for, as from the settings screen: a tug relay can check too
            cv.Optional(CONF_ON_FIRMWARE_CHECK): automation.validate_automation({}),
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
    if (position_link := config.get(CONF_POSITION_LINK_ID)) is not None:
        cg.add_define("USE_TUG_LOGGER_POSITION_LINK")
        cg.add(var.set_position_link(await cg.get_variable(position_link)))
    if (link := config.get(CONF_UPLOAD_LINK_ID)) is not None:
        cg.add_define("USE_TUG_LOGGER_UPLOAD_LINK")
        cg.add(var.set_upload_link(await cg.get_variable(link)))
        if CONF_LORA_ADDRESSING_ID in config:
            cg.add(
                var.set_lora_layers(
                    await cg.get_variable(config[CONF_LORA_ADDRESSING_ID]),
                    await cg.get_variable(config[CONF_LORA_ENCRYPTION_ID]),
                )
            )
    for key, method in (
        (CONF_ON_TAKEOFF, "add_on_takeoff_callback"),
        (CONF_ON_LANDED, "add_on_landed_callback"),
        (CONF_ON_POWER_OFF, "add_on_power_off_callback"),
        (CONF_ON_FIRMWARE_CHECK, "add_on_firmware_check_callback"),
    ):
        for conf in config.get(key, []):
            await automation.build_callback_automation(var, method, [], conf)
