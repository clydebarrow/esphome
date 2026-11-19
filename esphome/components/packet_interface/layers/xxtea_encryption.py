"""Encryption layer using XXTEA, with an optional rolling code."""

import hashlib

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_KEY
from esphome.cpp_generator import MockObj
from esphome.types import ConfigType

from . import (
    ENCRYPTION,
    Layer,
    PacketInterface,
    PacketSize,
    packet_interface_ns,
    remaining_packet_size,
)

CONF_ROLLING_CODE_ENABLE = "rolling_code_enable"
CONF_KEY_HEX = "key_hex"
CONF_KEY_AT_RUNTIME = "key_at_runtime"

# A captured packet lets a guessed key be checked offline, so the key text is stretched to make each guess costly.
# Both ends derive the same key from the same text, so the salt is fixed; it keeps tables built for other uses of
# PBKDF2 from applying here.
KEY_SALT = b"esphome packet_interface xxtea"
KEY_ITERATIONS = 600_000

# The check word, and with the rolling code the boot numbers and counter
CHECK_SIZE = 4
ROLLING_SIZE = 12

XxteaEncryption = packet_interface_ns.class_("XxteaEncryption", PacketInterface)


def _key_hex(value):
    """A random 256-bit key, as 64 hex digits, used as it is: it needs no stretching, unlike key text."""
    value = cv.string_strict(value).strip()
    try:
        key = bytes.fromhex(value)
    except ValueError as err:
        raise cv.Invalid("Must be 64 hex digits") from err
    if len(key) != 32:
        raise cv.Invalid("Must be 64 hex digits, for 32 bytes")
    return value


def _one_key(config: ConfigType) -> ConfigType:
    """Exactly one of the key text, the hex key, or a key set at run time by the application."""
    given = [key for key in (CONF_KEY, CONF_KEY_HEX) if key in config]
    if config[CONF_KEY_AT_RUNTIME]:
        given.append(CONF_KEY_AT_RUNTIME)
    if len(given) != 1:
        raise cv.Invalid(
            f"Set one of '{CONF_KEY}', '{CONF_KEY_HEX}' or '{CONF_KEY_AT_RUNTIME}: true'"
        )
    return config


XXTEA_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(XxteaEncryption),
            cv.Optional(CONF_KEY): cv.All(cv.sensitive(cv.string), cv.Length(min=1)),
            cv.Optional(CONF_KEY_HEX): cv.All(cv.sensitive(cv.string), _key_hex),
            # The application calls set_key(), as a tug does with the key in its provisioning file
            cv.Optional(CONF_KEY_AT_RUNTIME, default=False): cv.boolean,
            cv.Optional(CONF_ROLLING_CODE_ENABLE, default=False): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _one_key,
)


def derive_key(text: str) -> list[int]:
    """The 256-bit key for the key text, as 32-bit words in little endian order."""
    digest = hashlib.pbkdf2_hmac("sha256", text.encode(), KEY_SALT, KEY_ITERATIONS)
    return key_words(digest)


def key_words(key: bytes) -> list[int]:
    """The 256-bit key as 32-bit words, each from four bytes in little endian order."""
    return [int.from_bytes(key[i : i + 4], "little") for i in range(0, 32, 4)]


# The rolling code keeps the last counter of one peer, and accepts a packet only once the peer has sent back this
# device's boot number
@ENCRYPTION.register(
    "xxtea",
    schema=XXTEA_SCHEMA,
    single_peer=lambda config: config[CONF_ROLLING_CODE_ENABLE],
    needs_session=lambda config: config[CONF_ROLLING_CODE_ENABLE],
    auto_load=lambda config: ["xxtea"],
    # XXTEA works on whole 32-bit words
    packet_size=lambda config, size: remaining_packet_size(
        size & ~3,
        CHECK_SIZE + (ROLLING_SIZE if config[CONF_ROLLING_CODE_ENABLE] else 0),
    ),
)
async def build_xxtea(
    config: ConfigType, lower: MockObj, lower_size: PacketSize
) -> Layer:
    cg.add_define("USE_PACKET_INTERFACE_XXTEA")
    rolling_code = config[CONF_ROLLING_CODE_ENABLE]
    buffers = cg.static_buffer(
        config[CONF_ID],
        cg.uint32,
        XxteaEncryption.static_("buffer_words")(lower_size),
        psram=True,
    )
    var = cg.new_Pvariable(config[CONF_ID], lower, rolling_code, buffers)
    if CONF_KEY in config:
        cg.add(var.set_key(derive_key(config[CONF_KEY])))
    elif CONF_KEY_HEX in config:
        cg.add(var.set_key(key_words(bytes.fromhex(config[CONF_KEY_HEX]))))
    await cg.register_component(var, config)
    return var, XxteaEncryption.static_("packet_size")(lower_size, rolling_code)
