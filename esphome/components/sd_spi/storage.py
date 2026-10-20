from esphome import pins
import esphome.codegen as cg
from esphome.components import spi, storage
import esphome.config_validation as cv
from esphome.types import ConfigType

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["spi"]

CONF_CARD_DETECT_PIN = "card_detect_pin"

sd_spi_ns = cg.esphome_ns.namespace("sd_spi")
SdSpi = sd_spi_ns.class_("SdSpi", storage.BlockDevice, cg.Component, spi.SPIDevice)

CONFIG_SCHEMA = (
    storage.block_device_schema(SdSpi)
    .extend(
        {
            cv.Optional(CONF_CARD_DETECT_PIN): pins.gpio_input_pin_schema,
        }
    )
    .extend(spi.spi_device_schema(cs_pin_required=True, default_data_rate="20MHz"))
)

FINAL_VALIDATE_SCHEMA = spi.final_validate_device_schema(
    "sd_spi", require_mosi=True, require_miso=True
)


async def to_code(config: ConfigType) -> None:
    var = await storage.new_block_device(config)
    await spi.register_spi_device(var, config)
    await cg.add_pin(var.set_card_detect_pin, config.get(CONF_CARD_DETECT_PIN))
