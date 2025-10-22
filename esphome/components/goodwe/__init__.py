import esphome.codegen as cg
from esphome.components import transport
from esphome.components.transport import CONF_TRANSPORT
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import TimePeriod
from esphome.cpp_helpers import register_component

CODEOWNERS = ["@clydebarrow"]

AUTO_LOAD = ["bytebuffer"]

goodwe_ns = cg.esphome_ns.namespace("goodwe")
Goodwe = goodwe_ns.class_("Goodwe", cg.PollingComponent)
Parameter = goodwe_ns.class_("Parameter")

DATA_OFFSET = 7  # Offset for data in the Goodwe protocol (length of header)

CONF_GOODWE_ID = "goodwe_id"
CONF_CONFIGURE_ALL = "configure_all"
CONF_SENSOR_INTERVAL = "sensor_interval"
CONF_SETTINGS_INTERVAL = "settings_interval"
CONF_VERSION_INTERVAL = "version_interval"

COMMAND_SENSOR_DATA = 0x106
COMMAND_SETTINGS_DATA = 0x109
COMMAND_VERSION_DATA = 0x102

INTERVALS = (
    (CONF_SENSOR_INTERVAL, COMMAND_SENSOR_DATA, "1s"),
    (CONF_SETTINGS_INTERVAL, COMMAND_SETTINGS_DATA, "10s"),
    (CONF_VERSION_INTERVAL, COMMAND_VERSION_DATA, "60s"),
)

interval_validator_ = cv.All(
    cv.time_period, cv.Range(min=TimePeriod(milliseconds=500), max=TimePeriod(hours=1))
)


def validator_(config):
    sensor = config[CONF_SENSOR_INTERVAL].total_milliseconds
    settings = config[CONF_SETTINGS_INTERVAL].total_milliseconds
    version = config[CONF_VERSION_INTERVAL].total_milliseconds

    if settings <= sensor:
        raise cv.Invalid("Settings interval must be greater than sensor interval")
    if version <= settings:
        raise cv.Invalid("Version interval must be greater than settings interval")
    return config


CONFIG_SCHEMA = cv.All(
    cv.COMPONENT_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(Goodwe),
            cv.GenerateID(transport.CONF_TRANSPORT): cv.use_id(transport.Transport),
            **{cv.Optional(x[0], default=x[2]): interval_validator_ for x in INTERVALS},
        }
    ),
    validator_,
)


async def to_code(config):
    intervals = [(x[1], config[x[0]].total_milliseconds) for x in INTERVALS]
    intervals.sort(key=lambda x: x[1], reverse=True)
    base_interval = intervals[-1][1]
    tvar = await cg.get_variable(config[CONF_TRANSPORT])
    var = cg.new_Pvariable(config[CONF_ID], tvar)
    await register_component(var, config)
    cg.add(var.set_update_interval(base_interval))
    for code, interval in intervals:
        cg.add(var.add_query(code, int((interval + base_interval - 1) / base_interval)))
