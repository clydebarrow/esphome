from esphome import automation
import esphome.codegen as cg
from esphome.components import filesystem
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PATH
from esphome.core import CORE, ID
from esphome.types import ConfigType

CODEOWNERS = ["@esphome/core"]
# No DEPENDENCIES: `image: platform: file` loads this component too, and does not need a filesystem
DOMAIN = "file"
MULTI_CONF = True

CONF_MAX_PATH_LENGTH = "max_path_length"

file_ns = cg.esphome_ns.namespace("file")
FileRef = file_ns.class_("FileRef")


def get_filesystem_id(file_id: ID) -> ID:
    """Return the id of the filesystem that a `file:` entry is on."""
    for conf in CORE.config[DOMAIN]:
        if conf[CONF_ID] == file_id:
            return conf[filesystem.CONF_FILESYSTEM_ID]
    raise ValueError(f"No file entry with id {file_id}")


def _validate_path_length(config: ConfigType) -> ConfigType:
    if (path := config.get(CONF_PATH)) is not None and len(path) > config[
        CONF_MAX_PATH_LENGTH
    ]:
        raise cv.Invalid(
            f"The path is longer than {CONF_MAX_PATH_LENGTH} ({config[CONF_MAX_PATH_LENGTH]})",
            path=[CONF_PATH],
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FileRef),
            cv.GenerateID(filesystem.CONF_FILESYSTEM_ID): cv.use_id(
                filesystem.Filesystem
            ),
            cv.Optional(CONF_PATH): filesystem.validate_path,
            cv.Optional(CONF_MAX_PATH_LENGTH, default=64): cv.int_range(min=1, max=255),
        }
    ),
    _validate_path_length,
)

FILE_ID_SCHEMA = automation.maybe_simple_id({cv.GenerateID(): cv.use_id(FileRef)})

automation.register_apply_condition("file.exists", FILE_ID_SCHEMA, "exists()")
automation.register_apply_condition("file.is_file", FILE_ID_SCHEMA, "is_file()")
automation.register_apply_condition(
    "file.is_directory", FILE_ID_SCHEMA, "is_directory()"
)

automation.register_apply_action(
    "file.set_path",
    cv.maybe_simple_value(
        {
            cv.GenerateID(): cv.use_id(FileRef),
            cv.Required(CONF_PATH): cv.templatable(filesystem.validate_path),
        },
        key=CONF_PATH,
    ),
    automation.ApplyField(CONF_PATH, "set_path", cg.std_string),
)


async def to_code(config: ConfigType) -> None:
    cg.add_define("USE_FILE")
    fs = await cg.get_variable(config[filesystem.CONF_FILESYSTEM_ID])
    cg.new_Pvariable(
        config[CONF_ID],
        fs,
        config[CONF_MAX_PATH_LENGTH],
        config.get(CONF_PATH, cg.nullptr),
    )
