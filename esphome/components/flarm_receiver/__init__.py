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
    CONF_ADDRESS,
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

CONF_ADDRESS_TYPE = "address_type"
CONF_AIRCRAFT_TYPE = "aircraft_type"
CONF_BAND = "band"
CONF_NO_TRACK = "no_track"
CONF_REGISTRATION = "registration"
CONF_TRANSMIT = "transmit"
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

# FLARM address types and aircraft types
ADDRESS_TYPES = {"ICAO": 1, "FLARM": 2}
AIRCRAFT_TYPES = {
    "UNKNOWN": 0,
    "GLIDER": 1,
    "TOW_PLANE": 2,
    "HELICOPTER": 3,
    "SKYDIVER": 4,
    "DROP_PLANE": 5,
    "HANG_GLIDER": 6,
    "PARAGLIDER": 7,
    "POWERED": 8,
    "JET": 9,
    "BALLOON": 11,
    "AIRSHIP": 12,
    "UAV": 13,
    "STATIC_OBSTACLE": 15,
}

AUSTRALIAN_ADDRESS_BASE = 0x7C0000
REGISTRATION_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"


def australian_address(value: str) -> int:
    """The ICAO address of an Australian registration: 7C0000 plus its three characters in base 36."""
    value = cv.string_strict(value).upper().removeprefix("VH-").removeprefix("VH")
    if len(value) != 3 or any(ch not in REGISTRATION_CHARS for ch in value):
        raise cv.Invalid(
            "Give an Australian registration, such as VH-GKP: three letters or digits after VH-"
        )
    total = 0
    for ch in value:
        total = total * 36 + REGISTRATION_CHARS.index(ch)
    return AUSTRALIAN_ADDRESS_BASE + total


TRANSMIT_SCHEMA = cv.Schema(
    {
        # The aircraft's ICAO (Mode S) address, given as a hex number or, for an Australian aircraft, worked out
        # from its registration. Without either, the registration comes from XCSoar's club.json, or the unit
        # sends a FLARM ID made from its MAC address.
        cv.Exclusive(CONF_ADDRESS, CONF_ADDRESS): cv.All(
            cv.hex_int, cv.int_range(min=1, max=0xFFFFFF)
        ),
        cv.Exclusive(CONF_REGISTRATION, CONF_ADDRESS): australian_address,
        cv.Optional(CONF_ADDRESS_TYPE, default="ICAO"): cv.enum(
            ADDRESS_TYPES, upper=True
        ),
        cv.Optional(CONF_AIRCRAFT_TYPE, default="GLIDER"): cv.enum(
            AIRCRAFT_TYPES, upper=True, space="_"
        ),
        # Asks receivers such as OGN not to show the aircraft on the internet
        cv.Optional(CONF_NO_TRACK, default=False): cv.boolean,
    }
)

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
    if CONF_TRANSMIT in config and CONF_UART_ID not in config:
        raise cv.Invalid(
            f"'{CONF_TRANSMIT}' needs a GPS ('{CONF_UART_ID}'): the position sent comes only from a GPS fix"
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
            # Send this aircraft's position, so that FLARMs nearby see it
            cv.Optional(CONF_TRANSMIT): TRANSMIT_SCHEMA,
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
    if (transmit := config.get(CONF_TRANSMIT)) is not None:
        cg.add(
            var.set_transmit(
                transmit.get(CONF_ADDRESS, transmit.get(CONF_REGISTRATION, 0)),
                transmit[CONF_ADDRESS_TYPE],
                transmit[CONF_AIRCRAFT_TYPE],
                transmit[CONF_NO_TRACK],
            )
        )
