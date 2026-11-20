"""Joins a glider's SkyEcho over Wi-Fi without its name configured: the remembered one, or the strongest in range."""

import esphome.codegen as cg
from esphome.components import wifi
import esphome.config_validation as cv
from esphome.const import CONF_FAST_CONNECT, CONF_ID
import esphome.final_validate as fv
from esphome.types import ConfigType

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["skyecho", "wifi"]
DOMAIN = "skyecho_wifi"

CONF_SKYECHO_ID = "skyecho_id"
CONF_WEAK_SIGNAL = "weak_signal"

skyecho_wifi_ns = cg.esphome_ns.namespace("skyecho_wifi")
SkyEchoWifi = skyecho_wifi_ns.class_("SkyEchoWifi", cg.Component)
SkyEcho = cg.esphome_ns.namespace("skyecho").class_("SkyEcho", cg.PollingComponent)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SkyEchoWifi),
            cv.GenerateID(CONF_SKYECHO_ID): cv.use_id(SkyEcho),
            # In flight, a signal below this for a while drops the SkyEcho and scans again. The glider's own, a few
            # meters away in the cockpit, is about -40 to -60 dBm
            cv.Optional(CONF_WEAK_SIGNAL, default=-75): cv.int_range(min=-100, max=-40),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)


def _final_validate(config: ConfigType) -> ConfigType:
    # fast_connect goes back to the last network at every start, ahead of the choice
    fast_connect = fv.full_config.get()[wifi.DOMAIN].get(CONF_FAST_CONNECT, {})
    if fast_connect.get("enabled"):
        raise cv.Invalid(f"{DOMAIN} needs 'fast_connect: false' in the wifi block")
    # The chosen SkyEcho goes in the wifi component's list with the configured networks
    wifi.request_runtime_networks(1)
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_WEAK_SIGNAL])
    await cg.register_component(var, config)
    wifi.request_wifi_scan_results()
    wifi.request_wifi_scan_results_listener()
    wifi.request_wifi_connect_state_listener()
    await cg.add_variable(var.set_skyecho, config[CONF_SKYECHO_ID])
