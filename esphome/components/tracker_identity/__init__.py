"""A glider tracker's identity (LoRa address, key and registration) kept in flash, apart from the firmware.

One firmware build serves every tracker of a board type: the identity is written once over the USB serial port, by
the flight-upload Worker's tracker page, and an update leaves it alone. Until it is written, the tracker sends nothing
on LoRa. The port answers two lines, as the CubeCell tracker's does:

- ``ID``: ``ID {"project": ..., "build": ..., "set": ..., "address": ..., "registration": ..., "mac": ...}``, never
  the key.
- ``SET <registration> <address in hex> <64 hex digits of key>``: ``SET OK``, then a restart to apply it, or
  ``SET ERR <reason>``.
"""

import esphome.codegen as cg
from esphome.components.const import CONF_FLARM_RECEIVER_ID
from esphome.components.packet_interface.layers.basic_addressing import BasicAddressing
from esphome.components.packet_interface.layers.xxtea_encryption import XxteaEncryption
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.types import ConfigType

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["esp32", "logger", "packet_interface"]
DOMAIN = "tracker_identity"

CONF_ADDRESSING_ID = "addressing_id"
CONF_ENCRYPTION_ID = "encryption_id"
CONF_GLIDER_TRACKER_ID = "glider_tracker_id"
CONF_ESP32_BLE_ID = "esp32_ble_id"

tracker_identity_ns = cg.esphome_ns.namespace("tracker_identity")
TrackerIdentity = tracker_identity_ns.class_("TrackerIdentity", cg.Component)


def _flarm_receiver_id(value):
    # Imported here, so that a tracker without FLARM doesn't need the component
    from esphome.components.flarm_receiver import FlarmReceiver

    return cv.use_id(FlarmReceiver)(value)


def _glider_tracker_id(value):
    from esphome.components.glider_tracker import GliderTracker

    return cv.use_id(GliderTracker)(value)


def _esp32_ble_id(value):
    from esphome.components.esp32_ble import ESP32BLE

    return cv.use_id(ESP32BLE)(value)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TrackerIdentity),
        # The layers whose address and key come from the identity
        cv.Required(CONF_ADDRESSING_ID): cv.use_id(BasicAddressing),
        cv.Required(CONF_ENCRYPTION_ID): cv.use_id(XxteaEncryption),
        # Sends nothing until the identity is set
        cv.Optional(CONF_GLIDER_TRACKER_ID): _glider_tracker_id,
        # Transmits on FLARM as the registration's aircraft
        cv.Optional(CONF_FLARM_RECEIVER_ID): _flarm_receiver_id,
        # Named "<registration> FLARM"; a change takes effect at the restart after SET
        cv.Optional(CONF_ESP32_BLE_ID): _esp32_ble_id,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_addressing(await cg.get_variable(config[CONF_ADDRESSING_ID])))
    cg.add(var.set_encryption(await cg.get_variable(config[CONF_ENCRYPTION_ID])))
    if tracker := config.get(CONF_GLIDER_TRACKER_ID):
        cg.add_define("USE_TRACKER_IDENTITY_GLIDER_TRACKER")
        cg.add(var.set_glider_tracker(await cg.get_variable(tracker)))
    if flarm := config.get(CONF_FLARM_RECEIVER_ID):
        cg.add_define("USE_TRACKER_IDENTITY_FLARM")
        cg.add(var.set_flarm(await cg.get_variable(flarm)))
    if ble := config.get(CONF_ESP32_BLE_ID):
        cg.add_define("USE_TRACKER_IDENTITY_BLE")
        cg.add(var.set_ble(await cg.get_variable(ble)))
