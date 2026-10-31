import esphome.codegen as cg
from esphome.components import i2c
from esphome.components.audio_dac import AudioDac
import esphome.config_validation as cv
from esphome.types import ConfigType

CODEOWNERS = ["@P4uLT"]
CONF_ES8388_ID = "es8388_id"
CONF_VOLUME_MIN_DB = "volume_min_db"
CONF_VOLUME_MAX_DB = "volume_max_db"

es8388_ns = cg.esphome_ns.namespace("es8388")

ES8388 = es8388_ns.class_("ES8388", AudioDac, cg.Component, i2c.I2CDevice)

DEPENDENCIES = ["i2c"]

_VOLUME_DB = cv.All(cv.decibel, cv.float_range(min=-96.0, max=0.0))


def _validate_volume_range(config: ConfigType) -> ConfigType:
    if config[CONF_VOLUME_MIN_DB] >= config[CONF_VOLUME_MAX_DB]:
        raise cv.Invalid(f"{CONF_VOLUME_MIN_DB} must be less than {CONF_VOLUME_MAX_DB}")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ES8388),
            cv.Optional(CONF_VOLUME_MIN_DB, default="-96dB"): _VOLUME_DB,
            cv.Optional(CONF_VOLUME_MAX_DB, default="0dB"): _VOLUME_DB,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x10)),
    _validate_volume_range,
)


async def to_code(config: ConfigType) -> None:
    var = await i2c.new_i2c_component(config)
    cg.add(var.set_volume_min_db(config[CONF_VOLUME_MIN_DB]))
    cg.add(var.set_volume_max_db(config[CONF_VOLUME_MAX_DB]))
