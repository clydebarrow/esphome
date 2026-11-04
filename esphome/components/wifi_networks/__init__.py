import esphome.codegen as cg
from esphome.components import wifi
from esphome.components.const import CONF_ENABLED
import esphome.config_validation as cv
from esphome.const import CONF_FAST_CONNECT, CONF_ID
import esphome.final_validate as fv
from esphome.types import ConfigType

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["wifi"]
DOMAIN = "wifi_networks"

CONF_MAX_NETWORKS = "max_networks"
CONF_SKYECHO_ID = "skyecho_id"

wifi_networks_ns = cg.esphome_ns.namespace("wifi_networks")
WifiNetworks = wifi_networks_ns.class_("WifiNetworks", cg.Component)
# Named here rather than imported, so the skyecho component is only loaded by configs that use it
SkyEcho = cg.esphome_ns.namespace("skyecho").class_("SkyEcho", cg.PollingComponent)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(WifiNetworks),
            cv.Optional(CONF_MAX_NETWORKS, default=8): cv.int_range(min=1, max=32),
            cv.Optional(CONF_SKYECHO_ID): cv.use_id(SkyEcho),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)


def _final_validate(config: ConfigType) -> ConfigType:
    # fast_connect goes back to the last network at every start, ahead of the priorities
    fast_connect = fv.full_config.get()[wifi.DOMAIN].get(CONF_FAST_CONNECT, {})
    if fast_connect.get(CONF_ENABLED):
        raise cv.Invalid(f"{DOMAIN} needs 'fast_connect: false' in the wifi block")
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add_define("WIFI_NETWORKS_MAX", config[CONF_MAX_NETWORKS])
    wifi.request_wifi_scan_results()
    wifi.request_wifi_scan_results_listener()
    wifi.request_wifi_connect_state_listener()
    if (skyecho := config.get(CONF_SKYECHO_ID)) is not None:
        cg.add_define("USE_WIFI_NETWORKS_SKYECHO")
        await cg.add_variable(var.set_skyecho, skyecho)
