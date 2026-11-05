import esphome.codegen as cg
from esphome.components import sensor, socket, time as time_
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_PORT,
    CONF_PRESSURE,
    DEVICE_CLASS_PRESSURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_HECTOPASCAL,
)
from esphome.types import ConfigType

from .. import CONF_SKYECHO_ID, SkyEchoComponent, skyecho_ns

DEPENDENCIES = ["skyecho"]
AUTO_LOAD = ["sensor"]

SkyEchoTime = skyecho_ns.class_("SkyEchoTime", time_.RealTimeClock)

CONFIG_SCHEMA = cv.All(
    time_.TIME_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(SkyEchoTime),
            cv.GenerateID(CONF_SKYECHO_ID): cv.use_id(SkyEchoComponent),
            cv.Optional(CONF_PORT, default=80): cv.port,
            # The pressure from the SkyEcho's barometer, about once a second; the connection then stays open
            cv.Optional(CONF_PRESSURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_HECTOPASCAL,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_PRESSURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    ),
    socket.consume_sockets(1, "skyecho_time"),
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_PORT])
    await time_.register_time(var, config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_SKYECHO_ID])
    socket.require_tcp_client_link()
    if (pressure := config.get(CONF_PRESSURE)) is not None:
        cg.add(var.set_pressure_sensor(await sensor.new_sensor(pressure)))
