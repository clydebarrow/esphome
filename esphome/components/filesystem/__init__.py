from dataclasses import dataclass, field

import esphome.codegen as cg
from esphome.components.storage import CONF_STORAGE_ID, BlockDevice, request_lock
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE, ID, CoroPriority, EsphomeError, coroutine_with_priority
from esphome.cpp_generator import MockObj, MockObjClass
import esphome.final_validate as fv
from esphome.types import ConfigType, SafeExpType

CODEOWNERS = ["@clydebarrow"]
DEPENDENCIES = ["storage"]
DOMAIN = "filesystem"
IS_PLATFORM_COMPONENT = True

CONF_FILESYSTEM_ID = "filesystem_id"
CONF_FORMAT_IF_MOUNT_FAILED = "format_if_mount_failed"
CONF_MAX_OPEN_DIRS = "max_open_dirs"
CONF_MAX_OPEN_FILES = "max_open_files"
CONF_VFS_ID = "vfs_id"
CONF_VFS_PATH = "vfs_path"

MAX_OPEN_FILES = 32
MAX_OPEN_DIRS = 8
# Handles the C library has on a filesystem with vfs_path, before its users ask for more
VFS_OPEN_FILES = 4
VFS_OPEN_DIRS = 2
# ESP_VFS_PATH_MAX
VFS_PATH_MAX = 15

filesystem_ns = cg.esphome_ns.namespace("filesystem")
Filesystem = filesystem_ns.class_("Filesystem", cg.Component)
Vfs = filesystem_ns.class_("Vfs")

FILESYSTEM_ID_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_FILESYSTEM_ID): cv.use_id(Filesystem),
    }
)


@dataclass
class _Pools:
    var: MockObj
    files: int
    dirs: int
    vfs: MockObj | None = None


@dataclass
class FilesystemData:
    pools: dict[str, _Pools] = field(default_factory=dict)
    requests: dict[str, list[int]] = field(default_factory=dict)
    vfs_requests: dict[str, list[int]] = field(default_factory=dict)
    job_added: bool = False


def _get_data() -> FilesystemData:
    if DOMAIN not in CORE.data:
        CORE.data[DOMAIN] = FilesystemData()
    return CORE.data[DOMAIN]


def validate_path(value: object) -> str:
    """Validate an absolute path on a filesystem."""
    value = cv.string_strict(value)
    if not value.startswith("/"):
        raise cv.Invalid("The path must start with '/'")
    return value


def validate_vfs_path(value: object) -> str:
    """Validate the C library mount point of a filesystem, such as /sdcard."""
    value = validate_path(value)
    if len(value) < 2 or value.endswith("/") or "/" in value[1:]:
        raise cv.Invalid("The mount point must be one name after '/', such as /sdcard")
    if len(value) > VFS_PATH_MAX:
        raise cv.Invalid(f"The mount point can be at most {VFS_PATH_MAX} characters")
    return value


def request_open_handles(filesystem_id: ID, files: int = 1, dirs: int = 0) -> None:
    """Reserve file and directory handles that a consumer keeps open at the same time.

    Call it from to_code. The reserved handles are added to the filesystem's own max_open_files and
    max_open_dirs.
    """
    counts = _get_data().requests.setdefault(str(filesystem_id), [0, 0])
    counts[0] += files
    counts[1] += dirs


def request_vfs_handles(filesystem_id: ID, files: int, dirs: int = 0) -> None:
    """Reserve C library file and directory handles on a filesystem with vfs_path.

    Call it from to_code, for a component that opens files with open() or fopen(). The handles are added to
    the filesystem's own pools as well.
    """
    counts = _get_data().vfs_requests.setdefault(str(filesystem_id), [0, 0])
    counts[0] += files
    counts[1] += dirs
    request_open_handles(filesystem_id, files, dirs)


def require_vfs_path(filesystem_id: ID) -> None:
    """Final validation: fail unless the filesystem has vfs_path set."""
    if CONF_VFS_PATH not in fv.get_declaring_config(filesystem_id):
        raise cv.Invalid(
            f"Filesystem '{filesystem_id}' needs '{CONF_VFS_PATH}', such as /sdcard"
        )


def filesystem_schema(class_: MockObjClass) -> cv.Schema:
    """The base schema for a filesystem platform; `extend` it with the platform's own options."""
    return cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(class_),
            cv.GenerateID(CONF_STORAGE_ID): cv.use_id(BlockDevice),
            cv.Optional(CONF_MAX_OPEN_FILES, default=2): cv.int_range(
                min=0, max=MAX_OPEN_FILES
            ),
            cv.Optional(CONF_MAX_OPEN_DIRS, default=1): cv.int_range(
                min=0, max=MAX_OPEN_DIRS
            ),
            cv.Optional(CONF_FORMAT_IF_MOUNT_FAILED, default=False): cv.boolean,
            cv.GenerateID(CONF_VFS_ID): cv.declare_id(Vfs),
            cv.Optional(CONF_VFS_PATH): cv.All(cv.only_on_esp32, validate_vfs_path),
        }
    ).extend(cv.COMPONENT_SCHEMA)


@coroutine_with_priority(CoroPriority.FINAL)
async def _emit_pool_sizes() -> None:
    data = _get_data()
    for key, pools in data.pools.items():
        files, dirs = data.requests.get(key, (0, 0))
        files += pools.files
        dirs += pools.dirs
        if pools.vfs is not None:
            vfs_files, vfs_dirs = data.vfs_requests.get(key, (0, 0))
            vfs_files += VFS_OPEN_FILES
            vfs_dirs += VFS_OPEN_DIRS
            files += VFS_OPEN_FILES
            dirs += VFS_OPEN_DIRS
            cg.add(pools.vfs.set_pool_sizes(vfs_files, vfs_dirs))
        if files > MAX_OPEN_FILES or dirs > MAX_OPEN_DIRS:
            raise EsphomeError(
                f"Filesystem '{key}' needs {files} open files and {dirs} open directories, counting those "
                f"its users reserve; the limits are {MAX_OPEN_FILES} and {MAX_OPEN_DIRS}. Lower "
                f"{CONF_MAX_OPEN_FILES} or {CONF_MAX_OPEN_DIRS}"
            )
        cg.add(pools.var.set_pool_sizes(files, dirs))


async def new_filesystem(config: ConfigType, *args: SafeExpType) -> MockObj:
    """Create a filesystem platform component on its block device and register it."""
    device = await cg.get_variable(config[CONF_STORAGE_ID])
    var = cg.new_Pvariable(config[CONF_ID], device, *args)
    await cg.register_component(var, config)
    if config[CONF_FORMAT_IF_MOUNT_FAILED]:
        cg.add(var.set_format_if_mount_failed(True))
    data = _get_data()
    pools = _Pools(var, config[CONF_MAX_OPEN_FILES], config[CONF_MAX_OPEN_DIRS])
    data.pools[str(config[CONF_ID])] = pools
    if (vfs_path := config.get(CONF_VFS_PATH)) is not None:
        from esphome.components import esp32

        cg.add_define("USE_FILESYSTEM_VFS")
        request_lock()
        esp32.require_vfs_dir()
        pools.vfs = cg.new_Pvariable(config[CONF_VFS_ID], var, vfs_path)
        cg.add(var.set_vfs(pools.vfs))
    if not data.job_added:
        data.job_added = True
        CORE.add_job(_emit_pool_sizes)
    return var


@coroutine_with_priority(CoroPriority.CORE)
async def to_code(config: ConfigType) -> None:
    cg.add_define("USE_FILESYSTEM")
