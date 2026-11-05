import esphome.codegen as cg
from esphome.components import sx126x, uart
from esphome.components.const import CONF_BITRATE, CONF_CRC_ENABLE, CONF_MODULATION
from esphome.components.sx126x import (
    CONF_DEVIATION,
    CONF_PAYLOAD_LENGTH,
    CONF_SX126X_ID,
    CONF_SYNC_VALUE,
)
import esphome.config_validation as cv
from esphome.const import (
    CONF_ALTITUDE,
    CONF_FREQUENCY,
    CONF_ID,
    CONF_LATITUDE,
    CONF_LONGITUDE,
    CONF_TIMEOUT,
    CONF_UART_ID,
)
import esphome.final_validate as fv
from esphome.types import ConfigType

DOMAIN = "flarm_receiver"
CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["sx126x"]
AUTO_LOAD = ["uart"]

CONF_BAND = "band"
CONF_FLARM_RECEIVER_ID = "flarm_receiver_id"

flarm_receiver_ns = cg.esphome_ns.namespace("flarm_receiver")
FlarmReceiver = flarm_receiver_ns.class_(
    "FlarmReceiver", cg.Component, uart.UARTDevice, sx126x.SX126xListener
)
BandPlan = flarm_receiver_ns.struct("BandPlan")

# The legacy FLARM channels, from SoftRF's freqplan.h: base frequency, spacing, channels, hopping
BANDS = {
    "EU": (868_200_000, 200_000, 2, False),
    "US": (902_200_000, 400_000, 65, True),
    "AU": (917_000_000, 400_000, 24, True),
    "NZ": (869_250_000, 200_000, 1, False),
}

# What the radio must be set to for the legacy protocol: 100 kbit/s Manchester chips, with the CRC checked here
RADIO_SETTINGS = {
    CONF_MODULATION: "FSK",
    CONF_BITRATE: 100000,
    CONF_DEVIATION: 50000,
    CONF_PAYLOAD_LENGTH: 52,
    CONF_CRC_ENABLE: False,
    CONF_SYNC_VALUE: [0x55, 0x99, 0xA5, 0xA9, 0x55, 0x66, 0x65, 0x96],
}


def _validate_position(config: ConfigType) -> ConfigType:
    if (CONF_LATITUDE in config) != (CONF_LONGITUDE in config):
        raise cv.Invalid(
            f"Set both '{CONF_LATITUDE}' and '{CONF_LONGITUDE}', or neither"
        )
    if CONF_ALTITUDE in config and CONF_LATITUDE not in config:
        raise cv.Invalid(
            f"'{CONF_ALTITUDE}' needs '{CONF_LATITUDE}' and '{CONF_LONGITUDE}'"
        )
    if CONF_UART_ID not in config and CONF_LATITUDE not in config:
        raise cv.Invalid(
            f"Without a GPS ('{CONF_UART_ID}'), set the position with '{CONF_LATITUDE}' and '{CONF_LONGITUDE}'"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FlarmReceiver),
            cv.GenerateID(CONF_SX126X_ID): cv.use_id(sx126x.SX126x),
            cv.Required(CONF_BAND): cv.one_of(*BANDS, upper=True),
            # The GPS, whose sentences are passed on with the traffic
            cv.Optional(CONF_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Optional(CONF_LATITUDE): cv.float_range(min=-90, max=90),
            cv.Optional(CONF_LONGITUDE): cv.float_range(min=-180, max=180),
            cv.Optional(CONF_ALTITUDE): cv.float_range(min=-500, max=10000),
            cv.Optional(
                CONF_TIMEOUT, default="10s"
            ): cv.positive_time_period_milliseconds,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_position,
)


def _final_validate(config: ConfigType) -> ConfigType:
    full_config = fv.full_config.get()
    radio_path = full_config.get_path_for_id(config[CONF_SX126X_ID])[:-1]
    radio = full_config.get_config_for_path(radio_path)
    for key, value in RADIO_SETTINGS.items():
        if radio.get(key) != value:
            raise cv.Invalid(
                f"The legacy FLARM protocol needs the radio's '{key}' set to {value}",
                path=[CONF_SX126X_ID],
            )
    # The radio calibrates its image rejection for the frequency it starts on, so that must be in the band
    base, spacing, channels, _ = BANDS[config[CONF_BAND]]
    if not base <= radio[CONF_FREQUENCY] <= base + spacing * (channels - 1):
        raise cv.Invalid(
            f"Set the radio's '{CONF_FREQUENCY}' to a frequency in the {config[CONF_BAND]} band, "
            f"such as {base} Hz",
            path=[CONF_SX126X_ID],
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config: ConfigType) -> None:
    base, spacing, channels, hopping = BANDS[config[CONF_BAND]]
    plan = cg.StructInitializer(
        BandPlan,
        ("base_frequency", base),
        ("spacing", spacing),
        ("channels", channels),
        ("hopping", hopping),
    )
    var = cg.new_Pvariable(
        config[CONF_ID], await cg.get_variable(config[CONF_SX126X_ID]), plan
    )
    await cg.register_component(var, config)
    if (uart_id := config.get(CONF_UART_ID)) is not None:
        cg.add(var.set_uart_parent(await cg.get_variable(uart_id)))
    if CONF_LATITUDE in config:
        cg.add(
            var.set_fixed_position(
                config[CONF_LATITUDE],
                config[CONF_LONGITUDE],
                config.get(CONF_ALTITUDE, 0.0),
            )
        )
    cg.add(var.set_timeout(config[CONF_TIMEOUT]))
