import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import spi, sensor
from esphome.const import (
    DEVICE_CLASS_PRESSURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_HECTOPASCAL,
)

DEPENDENCIES = ["spi"]
CODEOWNERS = ["@clydebarrow"]

lps25hb_ns = cg.esphome_ns.namespace("lps25hb")
lps25hbComponent = lps25hb_ns.class_(
    "LPS25HBComponent", cg.PollingComponent, spi.SPIDevice
)


CONFIG_SCHEMA = (
    sensor.sensor_schema(
        lps25hbComponent,
        unit_of_measurement=UNIT_HECTOPASCAL,
        accuracy_decimals=3,
        device_class=DEVICE_CLASS_PRESSURE,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    .extend(cv.polling_component_schema("5s"))
    .extend(spi.spi_device_schema(True))
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
