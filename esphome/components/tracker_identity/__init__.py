"""A glider tracker's identity (LoRa address, key and registration) kept in flash, apart from the firmware.

One firmware build serves every tracker of a board type: the identity is written once by the flight-upload Worker's
tracker page, and an update leaves it alone. Until it is written, the tracker sends nothing on LoRa. On ESP32 it is
written over the USB serial port and kept in preferences. On nRF52 with the Adafruit bootloader it is in its own flash
page at 0xE9000, which the page writes as one more block of the set-up UF2; the update UF2 has no block there.

The port answers two lines, as the CubeCell tracker's does:

- ``ID``: ``ID {"project": ..., "build": ..., "set": ..., "address": ..., "registration": ..., "mac": ...}``, never
  the key.
- ``SET <registration> <address in hex> <64 hex digits of key>``: ``SET OK``, then a restart to apply it, or
  ``SET ERR <reason>``. On nRF52, always ``SET ERR``.
"""

import esphome.codegen as cg
from esphome.components.const import CONF_FLARM_RECEIVER_ID
from esphome.components.packet_interface.layers.basic_addressing import BasicAddressing
from esphome.components.packet_interface.layers.xxtea_encryption import XxteaEncryption
import esphome.config_validation as cv
from esphome.const import CONF_ID, PLATFORM_ESP32, PLATFORM_NRF52
from esphome.core import CORE
import esphome.final_validate as fv
from esphome.types import ConfigType

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["logger", "packet_interface"]
DOMAIN = "tracker_identity"

CONF_ADDRESSING_ID = "addressing_id"
CONF_ENCRYPTION_ID = "encryption_id"
CONF_GLIDER_TRACKER_ID = "glider_tracker_id"
CONF_ESP32_BLE_ID = "esp32_ble_id"

# The identity page on nRF52: the top page of the app area that a UF2 can write. On the nRF52840 the Adafruit
# bootloader takes UF2 blocks up to 0xEA000, below the ten pages it keeps for the app's data (the preferences)
IDENTITY_PAGE_ADDRESS = 0xE9000
IDENTITY_PAGE_SIZE = 0x1000
# Where the Adafruit bootloader starts on the nRF52840
BOOTLOADER_START = 0xF4000
# The bootloaders whose UF2 can reach that page
_UF2_BOOTLOADERS = ("adafruit_nrf52_sd140_v6", "adafruit_nrf52_sd140_v7")

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
        cv.Optional(CONF_ESP32_BLE_ID): cv.All(cv.only_on_esp32, _esp32_ble_id),
    }
).extend(cv.COMPONENT_SCHEMA)
CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, cv.only_on([PLATFORM_ESP32, PLATFORM_NRF52]))


def FINAL_VALIDATE_SCHEMA(config: ConfigType) -> ConfigType:
    if CORE.is_nrf52:
        bootloader = fv.full_config.get().get("nrf52", {}).get("bootloader")
        if bootloader not in _UF2_BOOTLOADERS:
            raise cv.Invalid(
                f"tracker_identity on nRF52 needs one of the bootloaders {', '.join(_UF2_BOOTLOADERS)}, not {bootloader}"
            )
        if CORE.using_toolchain_platformio:
            raise cv.Invalid(
                "tracker_identity on nRF52 needs the sdk-nrf toolchain, whose partition manager places its page"
            )
    return config


def _add_identity_partition() -> None:
    # Imported here, so that an ESP32 build doesn't need the Zephyr helpers
    from esphome.components.zephyr import Section, zephyr_add_pm_static

    # The partition manager fits the app below the page, so the linker refuses an app that would reach it. It needs
    # one gap for the app, so the preferences are placed too: in the pages the bootloader keeps for the app's data
    app_data_start = IDENTITY_PAGE_ADDRESS + IDENTITY_PAGE_SIZE
    zephyr_add_pm_static(
        [
            Section(
                "identity_partition",
                IDENTITY_PAGE_ADDRESS,
                IDENTITY_PAGE_SIZE,
                "flash_primary",
            ),
            Section(
                "settings_storage",
                app_data_start,
                BOOTLOADER_START - app_data_start,
                "flash_primary",
            ),
        ]
    )


async def to_code(config: ConfigType) -> None:
    if CORE.is_nrf52:
        _add_identity_partition()
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
