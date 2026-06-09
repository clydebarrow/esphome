import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_BATTERY_CHARGING, DEVICE_CLASS_PLUG

from . import AXP2101_SUB_SCHEMA, CONF_AXP2101_ID

DEPENDENCIES = ["axp2101"]

CONF_CHARGING = "charging"
CONF_BATTERY_PRESENT = "battery_present"
CONF_VBUS_PRESENT = "vbus_present"
CONF_CHARGE_DONE = "charge_done"

CONFIG_SCHEMA = AXP2101_SUB_SCHEMA.extend(
    {
        cv.Optional(CONF_CHARGING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY_CHARGING,
        ),
        cv.Optional(CONF_BATTERY_PRESENT): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_VBUS_PRESENT): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PLUG,
        ),
        cv.Optional(CONF_CHARGE_DONE): binary_sensor.binary_sensor_schema(),
    }
)

SETTERS = {
    CONF_CHARGING: "set_charging_binary_sensor",
    CONF_BATTERY_PRESENT: "set_battery_present_binary_sensor",
    CONF_VBUS_PRESENT: "set_vbus_present_binary_sensor",
    CONF_CHARGE_DONE: "set_charge_done_binary_sensor",
}


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AXP2101_ID])
    for key, setter in SETTERS.items():
        if conf := config.get(key):
            sens = await binary_sensor.new_binary_sensor(conf)
            cg.add(getattr(parent, setter)(sens))
