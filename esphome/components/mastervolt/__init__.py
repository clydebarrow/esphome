import esphome.codegen as cg
from esphome.components.canbus import CONF_CANBUS_ID, CanbusComponent
import esphome.config_validation as cv
from esphome.const import CONF_DEBUG, CONF_DEVICE_ID, CONF_DEVICES, CONF_ID, CONF_TYPE

CODEOWNERS = ["@clydebarrow"]

DOMAIN = "mastervolt"

DEPENDENCIES = ["canbus"]

AUTO_LOAD = ["bytebuffer", "sensor"]

CONF_IDAL = "idal"
CONF_IDB = "idb"
CONF_ANNOUNCE = "announce"
CONF_OWN_IDB = "own_idb"
CONF_OWN_IDAL = "own_idal"
CONF_MAX_DEVICES = "max_devices"
CONF_OFFLINE_TIMEOUT = "offline_timeout"

# Must match offline_timeout_ms_ in mastervolt.h
DEFAULT_OFFLINE_TIMEOUT_MS = 45000

# Known MasterBus device classes (IDAL values)
DEVICE_TYPES = {
    "charger": 0x0A,
    "dc_dc": 0x18,
    "dc_shunt": 0x13,
    "display": 0x14,
    "switch_output": 0x0E,
}

# Must match IDB_UNASSIGNED in mastervolt.h
IDB_UNASSIGNED = 0xFFFFFFFF

mastervolt_ns = cg.esphome_ns.namespace("mastervolt")
Mastervolt = mastervolt_ns.class_("Mastervolt", cg.Component)
MastervoltDevice = mastervolt_ns.class_("MastervoltDevice")
MastervoltSensor = mastervolt_ns.class_("MastervoltSensor")


def _device_idal(device_config) -> int:
    if (idal := device_config.get(CONF_IDAL)) is not None:
        return idal
    # cv.enum returns the key string, not the mapped value
    return DEVICE_TYPES[device_config[CONF_TYPE]]


def _validate(config):
    by_idal: dict[int, list] = {}
    for device_config in config[CONF_DEVICES]:
        by_idal.setdefault(_device_idal(device_config), []).append(device_config)
    for idal, device_configs in by_idal.items():
        if len(device_configs) > 1 and any(CONF_IDB not in c for c in device_configs):
            raise cv.Invalid(
                f"Multiple devices share the same class (IDAL 0x{idal:02X}); "
                f"each of them must specify '{CONF_IDB}'"
            )
    if config[CONF_ANNOUNCE] and CONF_OWN_IDB not in config:
        raise cv.Invalid(f"'{CONF_OWN_IDB}' is required when '{CONF_ANNOUNCE}' is on")
    return config


DEVICE_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MastervoltDevice),
            cv.Exclusive(CONF_TYPE, "device_class"): cv.enum(DEVICE_TYPES, lower=True),
            cv.Exclusive(CONF_IDAL, "device_class"): cv.int_range(min=0, max=0x1F),
            cv.Optional(CONF_IDB): cv.int_range(min=0, max=0x3FFFF),
        }
    ),
    cv.has_at_least_one_key(CONF_TYPE, CONF_IDAL),
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Mastervolt),
            cv.GenerateID(CONF_CANBUS_ID): cv.use_id(CanbusComponent),
            cv.Required(CONF_DEVICES): cv.ensure_list(DEVICE_SCHEMA),
            cv.Optional(CONF_ANNOUNCE, default=False): cv.boolean,
            cv.Optional(CONF_OWN_IDB): cv.int_range(min=0, max=0x3FFFF),
            # Announce as a display (0x14) by default - the closest class for a
            # monitoring device, so other bus devices know how to treat us
            cv.Optional(CONF_OWN_IDAL, default=0x14): cv.int_range(min=0, max=0x1F),
            cv.Optional(CONF_MAX_DEVICES, default=16): cv.int_range(min=1, max=64),
            cv.Optional(
                CONF_OFFLINE_TIMEOUT, default="45s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_DEBUG, default=False): cv.boolean,
        }
    ),
    _validate,
)


MASTERVOLT_DEVICE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_DEVICE_ID): cv.use_id(MastervoltDevice),
    }
)


async def to_code(config):
    canbus = await cg.get_variable(config[CONF_CANBUS_ID])
    var = cg.new_Pvariable(config[CONF_ID], canbus)
    await cg.register_component(var, config)

    cg.add_define("MASTERVOLT_MAX_DEVICES", config[CONF_MAX_DEVICES])
    timeout = config[CONF_OFFLINE_TIMEOUT]
    if timeout.total_milliseconds != DEFAULT_OFFLINE_TIMEOUT_MS:
        cg.add(var.set_offline_timeout(timeout))
    if config[CONF_DEBUG]:
        cg.add(var.set_debug(True))
    if config[CONF_ANNOUNCE]:
        cg.add(var.set_announce(config[CONF_OWN_IDB], config[CONF_OWN_IDAL]))

    for device_config in config[CONF_DEVICES]:
        device = cg.new_Pvariable(
            device_config[CONF_ID],
            _device_idal(device_config),
            device_config.get(CONF_IDB, IDB_UNASSIGNED),
        )
        cg.add(var.add_device(device))
