from esphome import automation
import esphome.codegen as cg
from esphome.components import file, runtime_image
from esphome.components.image import CONF_TRANSPARENCY, add_metadata
from esphome.components.runtime_image import IMAGE_FORMATS
from esphome.components.storage import storage_size
import esphome.config_validation as cv
from esphome.const import CONF_FORMAT, CONF_ID, CONF_ON_ERROR, CONF_PATH, CONF_TYPE
from esphome.types import ConfigType

from .. import (
    CONF_FILESYSTEM_ID,
    Filesystem,
    filesystem_ns,
    request_open_handles,
    validate_path,
)

AUTO_LOAD = ["runtime_image"]
CODEOWNERS = ["@clydebarrow"]

CONF_CHUNK_SIZE = "chunk_size"
CONF_FILE_ID = "file_id"
CONF_ON_LOAD_FINISHED = "on_load_finished"

FilesystemImage = filesystem_ns.class_(
    "FilesystemImage", cg.PollingComponent, runtime_image.RuntimeImage
)


def _default_filesystem(config: ConfigType) -> ConfigType:
    # A file names its own filesystem; otherwise pick the only one when none is given, as GenerateID would
    if CONF_FILE_ID in config:
        if CONF_FILESYSTEM_ID in config:
            raise cv.Invalid(
                f"{CONF_FILESYSTEM_ID} cannot be used with {CONF_FILE_ID}; the file sets it",
                path=[CONF_FILESYSTEM_ID],
            )
    elif CONF_FILESYSTEM_ID not in config:
        config[CONF_FILESYSTEM_ID] = None
    return config


CONFIG_SCHEMA = cv.All(
    _default_filesystem,
    runtime_image.runtime_image_schema(FilesystemImage)
    .extend(
        {
            cv.Optional(CONF_FILESYSTEM_ID): cv.use_id(Filesystem),
            cv.Optional(CONF_PATH): validate_path,
            cv.Optional(CONF_FILE_ID): cv.use_id(file.FileRef),
            # AUTO tells the format from the first bytes of the file, at the cost of building every decoder
            cv.Required(CONF_FORMAT): cv.one_of(*IMAGE_FORMATS, "AUTO", upper=True),
            cv.Optional(CONF_CHUNK_SIZE, default=4096): cv.All(
                storage_size, cv.int_range(min=256, max=65536)
            ),
            cv.Optional(CONF_ON_LOAD_FINISHED): automation.validate_automation({}),
            cv.Optional(CONF_ON_ERROR): automation.validate_automation({}),
        }
    )
    .extend(cv.polling_component_schema("never")),
    cv.has_at_most_one_key(CONF_PATH, CONF_FILE_ID),
    runtime_image.validate_runtime_image_settings,
)

LOAD_IMAGE_SCHEMA = cv.maybe_simple_value(
    {
        cv.GenerateID(): cv.use_id(FilesystemImage),
        cv.Required(CONF_PATH): cv.templatable(validate_path),
    },
    key=CONF_PATH,
)

RELEASE_IMAGE_SCHEMA = automation.maybe_simple_id(
    {
        cv.GenerateID(): cv.use_id(FilesystemImage),
    }
)

automation.register_apply_action(
    "filesystem.load_image",
    LOAD_IMAGE_SCHEMA,
    automation.ApplyField(CONF_PATH, "load", cg.std_string),
)

automation.register_apply_action(
    "filesystem.release_image",
    RELEASE_IMAGE_SCHEMA,
    automation.ApplyCall("release()"),
)

_CALLBACK_AUTOMATIONS = (
    automation.CallbackAutomation(
        CONF_ON_LOAD_FINISHED, "add_on_load_finished_callback"
    ),
    automation.CallbackAutomation(CONF_ON_ERROR, "add_on_error_callback"),
)


async def to_code(config: ConfigType) -> None:
    settings = await runtime_image.process_runtime_image_config(config)
    add_metadata(
        config[CONF_ID],
        settings.width,
        settings.height,
        config[CONF_TYPE],
        config[CONF_TRANSPARENCY],
    )
    if (file_id := config.get(CONF_FILE_ID)) is not None:
        fs_id = file.get_filesystem_id(file_id)
    else:
        fs_id = config[CONF_FILESYSTEM_ID]
    fs = await cg.get_variable(fs_id)
    var = cg.new_Pvariable(
        config[CONF_ID],
        fs,
        config.get(CONF_PATH, cg.nullptr),
        settings.width,
        settings.height,
        settings.format_enum,
        settings.image_type_enum,
        settings.transparent,
        settings.placeholder or cg.nullptr,
        config[CONF_CHUNK_SIZE],
        settings.byte_order_big_endian,
    )
    await cg.register_component(var, config)
    await cg.add_variable(var.set_file, config.get(CONF_FILE_ID))
    request_open_handles(fs_id, files=1)
    await automation.build_callback_automations(var, config, _CALLBACK_AUTOMATIONS)
