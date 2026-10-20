import esphome.codegen as cg
from esphome.components import esp32, filesystem
import esphome.config_validation as cv
from esphome.types import ConfigType

CODEOWNERS = ["@clydebarrow"]

fatfs_ns = cg.esphome_ns.namespace("fatfs")
FatFs = fatfs_ns.class_("FatFs", filesystem.Filesystem)

CONFIG_SCHEMA = cv.All(filesystem.filesystem_schema(FatFs), cv.only_on_esp32)


async def to_code(config: ConfigType) -> None:
    esp32.require_fatfs()
    await filesystem.new_filesystem(config)
