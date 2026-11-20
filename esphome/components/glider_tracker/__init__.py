"""A glider's tracker for the club: positions for the live map, and its flights reported to the clubhouse."""

from esphome import automation
import esphome.codegen as cg
from esphome.components import gps, sensor
from esphome.components.gps import CONF_GPS_ID
from esphome.components.packet_interface.layers.basic_addressing import AddressingPort
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.types import ConfigType

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["gps", "packet_interface"]
DOMAIN = "glider_tracker"

CONF_BATTERY_ID = "battery_id"
CONF_BROADCAST_ID = "broadcast_id"
CONF_CLUBHOUSE_ID = "clubhouse_id"
CONF_FLARM_ON_GROUND = "flarm_on_ground"
CONF_FLARM_RECEIVER_ID = "flarm_receiver_id"
CONF_ON_LANDING = "on_landing"
CONF_ON_TAKEOFF = "on_takeoff"

glider_tracker_ns = cg.esphome_ns.namespace("glider_tracker")
GliderTracker = glider_tracker_ns.class_("GliderTracker", cg.Component)


def _flarm_receiver_id(value):
    # Imported here, so that a tracker without FLARM doesn't need the component
    from esphome.components.flarm_receiver import FlarmReceiver

    return cv.use_id(FlarmReceiver)(value)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GliderTracker),
        cv.GenerateID(CONF_GPS_ID): cv.use_id(gps.GPS),
        # The encrypted peer that reaches the clubhouse receiver, and the broadcast peer for positions
        cv.Required(CONF_CLUBHOUSE_ID): cv.use_id(AddressingPort),
        cv.Required(CONF_BROADCAST_ID): cv.use_id(AddressingPort),
        cv.Optional(CONF_BATTERY_ID): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_FLARM_RECEIVER_ID): _flarm_receiver_id,
        # Listen for FLARM on the ground too, as for tests on the bench
        cv.Optional(CONF_FLARM_ON_GROUND, default=False): cv.boolean,
        cv.Optional(CONF_ON_TAKEOFF): automation.validate_automation({}),
        cv.Optional(CONF_ON_LANDING): automation.validate_automation({}),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_gps(await cg.get_variable(config[CONF_GPS_ID])))
    cg.add(var.set_clubhouse(await cg.get_variable(config[CONF_CLUBHOUSE_ID])))
    cg.add(var.set_broadcast(await cg.get_variable(config[CONF_BROADCAST_ID])))
    if battery := config.get(CONF_BATTERY_ID):
        cg.add(var.set_battery(await cg.get_variable(battery)))
    if flarm := config.get(CONF_FLARM_RECEIVER_ID):
        cg.add_define("USE_GLIDER_TRACKER_FLARM")
        cg.add(var.set_flarm(await cg.get_variable(flarm)))
    cg.add(var.set_flarm_on_ground(config[CONF_FLARM_ON_GROUND]))
    for key, method in (
        (CONF_ON_TAKEOFF, "add_on_takeoff_callback"),
        (CONF_ON_LANDING, "add_on_landing_callback"),
    ):
        for conf in config.get(key, []):
            await automation.build_callback_automation(var, method, [], conf)
