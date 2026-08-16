import hashlib
import re
from dataclasses import dataclass, field

from esphome import automation, preferences
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_DEVICE,
    CONF_FORMAT,
    CONF_ID,
    CONF_PATH,
    CONF_STORAGE,
)
from esphome.core import CORE, ID, coroutine_with_priority
import esphome.final_validate as fv
from esphome.coroutine import CoroPriority
from esphome.types import ConfigType

CODEOWNERS = ["@esphome/core"]

preferences_ns = cg.esphome_ns.namespace("preferences")
esp32_ns = cg.esphome_ns.namespace("esp32")
storage_ns = cg.esphome_ns.namespace("storage")
binary_storage_ns = cg.esphome_ns.namespace("binary_storage")
NVSStore = binary_storage_ns.class_("NVSStore")
RawStorage = storage_ns.class_("RawStorage")
IntervalSyncer = preferences_ns.class_("IntervalSyncer", cg.Component)

CONF_FLASH_WRITE_INTERVAL = "flash_write_interval"
CONF_RTC_STORAGE = "rtc_storage"
CONF_STORAGE_BACKEND = "storage_backend"
CONF_EXTERNAL_NVS = "external_nvs"
CONF_KEEP_EARLY = "keep_early"
CONF_USE_INTERNAL_NVS = "use_internal_nvs"


DOMAIN = "preferences"


@dataclass
class _PrefBackupData:
    # Raw preference regions per device id: every export/import action's address, plus the
    # container size when it can be computed. Filled while the actions are built and resolved
    # once at the end -- see _resolve_raw_pref_regions().
    raw_pref_regions: dict = field(default_factory=dict)
    raw_pref_job_queued: bool = False
    sensor_pref_job_queued: bool = False
    # Set by an action using format: json so the config-taking AUTO_LOAD pulls json in only
    # when it is actually used.
    json_required: bool = False
    # Set when any preferences backup action is used, so the config-taking AUTO_LOAD pulls in the
    # storage + binary_storage components the engine and the NVSStore view depend on.
    backup_used: bool = False


def _get_pref_data() -> _PrefBackupData:
    return CORE.data.setdefault(DOMAIN, _PrefBackupData())


def AUTO_LOAD(config: ConfigType) -> list[str]:
    # Route the flash preferences through the storage KeyValueStorage interface + NVS backend, but
    # only pull those components into the build when the option is actually enabled. json is pulled
    # in only when a preferences backup action uses format: json (flagged during its validation,
    # read here by this deferred config-taking AUTO_LOAD).
    loads: list[str] = []
    if config and (config.get(CONF_STORAGE_BACKEND) or config.get(CONF_EXTERNAL_NVS)):
        loads += ["storage", "binary_storage"]
    data = CORE.data.get(DOMAIN)
    if data is not None and data.backup_used:
        # The backup engine uses the storage interfaces, and the KeyValueStorage view it reads
        # through is a binary_storage NVSStore.
        for dep in ("storage", "binary_storage"):
            if dep not in loads:
                loads.append(dep)
    if data is not None and data.json_required:
        loads.append("json")
    return loads


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(IntervalSyncer),
        cv.Optional(CONF_FLASH_WRITE_INTERVAL, default="60s"): cv.update_interval,
        # Compile the RTC-backed storage into the ESP32 preferences backend even
        # when no other option selects it, so components (including external
        # ones) requesting in_flash=false are honoured instead of falling back
        # to NVS. No default: absence means "no request" (see
        # preferences.validate_rtc_storage for the per-platform rules).
        cv.Optional(CONF_RTC_STORAGE): preferences.validate_rtc_storage,
        # esp32 only: route the flash (NVS) preference path through the storage
        # KeyValueStorage interface instead of raw nvs_* calls. Format-identical
        # (same system "esphome" namespace); the RTC path is unaffected.
        cv.Optional(CONF_STORAGE_BACKEND): cv.All(cv.boolean, cv.only_on_esp32),
        # esp32 only: route the flash preference path to an external esp_partition NVS store
        # (a `format: nvs` region on external flash). Flash-backed preferences created after the
        # external store is bound live there; the RTC path (in_flash=false) is unaffected. The
        # referenced id is the NVS region's id.
        cv.Optional(CONF_EXTERNAL_NVS): cv.All(cv.use_id(NVSStore), cv.only_on_esp32),
        # esp32 only: components that read preferences before the storage stage (and so cannot use
        # the external store) -- their preferences are kept early (RTC). Validation of size/timing
        # is handled separately; listing here marks the intended exceptions.
        cv.Optional(CONF_KEEP_EARLY): cv.All(
            cv.ensure_list(cv.use_id(cg.Component)), cv.only_on_esp32
        ),
        # esp32 only: keep the internal system "esphome" NVS as a preferences backend. Set false
        # only with external_nvs and when every pre-storage reader uses RTC (keep_early) -- the
        # internal namespace is then not opened and preferences read before the external store is
        # bound return defaults. nvs_flash_init() still runs (IDF wifi/BT may need it).
        cv.Optional(CONF_USE_INTERNAL_NVS, default=True): cv.All(
            cv.boolean, cv.only_on_esp32
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def _final_validate(config):
    # With external_nvs, flash-backed preferences route to the external esp_partition NVS. The
    # safe_mode boot-loop counter defaults to flash: if it lands on the external store and that
    # store is unavailable at boot (SPI/chip fault, not yet mounted), the counter can never be
    # reset -> boot loop, defeating the very protection safe_mode provides. Force it onto RTC.
    if config.get(CONF_EXTERNAL_NVS) is None:
        return config
    safe_mode = fv.full_config.get().get("safe_mode")
    if safe_mode is not None and safe_mode.get(CONF_STORAGE, "flash") == "flash":
        raise cv.Invalid(
            "with 'external_nvs' the safe_mode boot-loop counter would be stored on the external "
            "NVS; if that store is unavailable at boot the counter cannot be reset, causing a boot "
            "loop. Set 'safe_mode: storage: rtc' (or remove 'external_nvs').",
            path=[CONF_EXTERNAL_NVS],
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


@coroutine_with_priority(CoroPriority.PREFERENCES)
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    write_interval = config[CONF_FLASH_WRITE_INTERVAL]
    if write_interval.total_milliseconds == 0:
        cg.add_define("USE_PREFERENCES_SYNC_EVERY_LOOP")
    else:
        cg.add(var.set_write_interval(write_interval))
    if config.get(CONF_RTC_STORAGE):
        preferences.request_rtc_storage()
    if config.get(CONF_STORAGE_BACKEND):
        cg.add_define("USE_ESP32_PREFERENCES_STORAGE")
        # FILTER_SOURCE_FILES in binary_storage is exclude-based: keep nvs_store.cpp
        # compiled even though no `type: nvs` device is configured.
        CORE.data.setdefault("binary_storage_device_types", set()).add("nvs")
    if config.get(CONF_KEEP_EARLY) and config.get(CONF_EXTERNAL_NVS) is None:
        raise cv.Invalid(
            f"'{CONF_KEEP_EARLY}' only applies together with '{CONF_EXTERNAL_NVS}': it marks the "
            f"components that must NOT use the external store (they read before it is available). "
            f"Those components route their preferences to RTC via their own in_flash/store option; "
            f"this list is the validated intent, not an automatic router."
        )
    if not config.get(CONF_USE_INTERNAL_NVS, True):
        if config.get(CONF_EXTERNAL_NVS) is None:
            raise cv.Invalid(
                f"'{CONF_USE_INTERNAL_NVS}: false' needs '{CONF_EXTERNAL_NVS}' -- without an "
                f"external store there is no flash preferences backend left"
            )
        cg.add_define("USE_ESP32_PREFERENCES_NO_INTERNAL")
    if (external := config.get(CONF_EXTERNAL_NVS)) is not None:
        # Route flash preferences through the external esp_partition NVS store. Reuses the same
        # KeyValueStorage seam as storage_backend, so save/load/sync are unchanged.
        cg.add_define("USE_ESP32_PREFERENCES_STORAGE")
        CORE.data.setdefault("binary_storage_device_types", set()).add("nvs")
        store = await cg.get_variable(external)
        cg.add(esp32_ns.set_external_preferences_store(store))
    await cg.register_component(var, config)


# preferences.export / preferences.import: back up ESPHome preferences (the
# "esphome" NVS namespace) to a storage target and restore them. The C++ only
# compiles in when one of these actions is used (USE_PREFERENCES_BACKUP), and
# only on esp32. Backup reads/writes the live preferences through the active
# KeyValueStorage view (esp32::get_preferences_store()), so it works on plain
# internal NVS as well as an external/storage-backed preferences store.
#
# Selection is an option on the action: `preferences:` lists global IDs --
# with it, only those entries round-trip and export under their YAML id;
# without it, the whole namespace round-trips under numeric NVS keys.

CONF_PREFERENCES = "preferences"
CONF_REBOOT = "reboot"

# Keep in sync with globals_component.h ("1944399030U ^ this->name_hash_")
# and the md5-based name hash in globals/__init__.py.
_GLOBALS_KEY_XOR = 1944399030

# YAML `type:` -> (PrefType tag, element size irrelevant here). Blob layouts:
# scalars/arrays are raw T bytes; std::string is length-prefixed char[SZ]
# with SZ = max_restore_data_length (default 63) + 1 -- keep in sync with
# globals/__init__.py.
_PREF_SCALAR_TYPES = {
    "bool": "BOOL",
    "int8_t": "I8",
    "char": "I8",
    "uint8_t": "U8",
    "unsigned char": "U8",
    "int16_t": "I16",
    "short": "I16",
    "uint16_t": "U16",
    "unsigned short": "U16",
    "int": "I32",
    "int32_t": "I32",
    "long": "I32",
    "uint32_t": "U32",
    "unsigned int": "U32",
    "unsigned long": "U32",
    "size_t": "U32",
    "float": "F32",
    "double": "F64",
}
_ARRAY_TYPE_RE = re.compile(r"^\s*(.+?)\s*\[\s*(\d+)\s*\]\s*$")

_RESTORING_RE = re.compile(r"RestoringGlobalsComponent<\s*(.+?)\s*>\s*$")
_RESTORING_STRING_RE = re.compile(
    r"RestoringGlobalStringComponent<\s*.+?,\s*(\d+)\s*>\s*$"
)


# Sensor platforms whose restore type codegen can name but the runtime sweep cannot: they all
# arrive as sensor::Sensor in App's list, four bytes wide, and a build without RTTI cannot tell
# them apart. The platform is right there in the YAML, so map the registered class to the kind
# and let register_entity_pref() carry it over. A platform that is not listed here keeps the
# sweep's RAW entry -- named, hex value -- which is the safe fallback: a wrong kind would render
# a wrong number AND write wrong bytes back on import.
_SENSOR_PREF_KINDS = {
    "total_daily_energy::TotalDailyEnergy": "FLOAT",
    "integration::IntegrationSensor": "FLOAT",
    "duty_time_sensor::DutyTimeSensor": "U32",
    "rotary_encoder::RotaryEncoderSensor": "I32",
}


# Preferences owned by a component rather than an entity: (component, C++ symbol, exported
# name, kind). Emitted only when that component is configured, and by SYMBOL -- the value stays
# in the owning component's header, so a rename breaks the build instead of silently exporting a
# number that has moved on. Without this they show up as a bare key, since the sweep only walks
# entities.
_COMPONENT_PREF_KEYS = (("safe_mode", "safe_mode::RTC_KEY", "safe_mode", "U32"),)


async def _register_component_prefs() -> None:
    """Names the component-owned preferences whose owners are part of this build."""
    for component, symbol, name, kind in _COMPONENT_PREF_KEYS:
        if component not in CORE.config:
            continue
        cg.add(
            cg.RawExpression(
                f'{preferences_ns}::register_key_pref({symbol}, "{name}", '
                f"{preferences_ns}::EntityKind::{kind})"
            )
        )


async def _register_typed_sensors() -> None:
    """Emits one register_entity_pref() per sensor whose restore type is known.

    FINAL priority: every sensor platform's own to_code() must have registered its variable
    before CORE.variables can be walked for them.
    """
    for reg_id in CORE.variables:
        # Registered types carry no esphome:: prefix; tolerate one anyway.
        type_str = str(reg_id.type).removeprefix("esphome::")
        kind = _SENSOR_PREF_KINDS.get(type_str)
        if kind is None:
            continue
        var = await cg.get_variable(reg_id)
        cg.add(
            cg.RawExpression(
                f"{preferences_ns}::register_entity_pref({var}, "
                f"{preferences_ns}::EntityKind::{kind})"
            )
        )


def _pref_type_from_class(type_str: str) -> tuple[str, int] | None:
    """(PrefType tag, count) from a declared global's C++ class string --
    codegen-world data only (ID.type of the registered variable). None when
    the class is not a restoring global at all."""
    if m := _RESTORING_STRING_RE.search(type_str):
        return "STRING", int(m.group(1))  # SZ straight from the template arg
    if m := _RESTORING_RE.search(type_str):
        inner = m.group(1)
        if inner in _PREF_SCALAR_TYPES:
            return _PREF_SCALAR_TYPES[inner], 1
        if am := _ARRAY_TYPE_RE.match(inner):
            base, count = am.group(1), int(am.group(2))
            if base in _PREF_SCALAR_TYPES:
                return _PREF_SCALAR_TYPES[base], count
        return (
            "HEX_FALLBACK",
            0,
        )  # restoring, but a type we cannot render -- hex round-trip
    return None


ExportPreferencesAction = preferences_ns.class_("ExportPreferencesAction", automation.Action)
ImportPreferencesAction = preferences_ns.class_("ImportPreferencesAction", automation.Action)


def _global_nvs_key(global_id: str) -> int:
    name_hash = int(hashlib.md5(global_id.encode()).hexdigest()[:8], 16)
    return (_GLOBALS_KEY_XOR ^ name_hash) & 0xFFFFFFFF


_PREFERENCES_ACTION_BASE = {
    cv.Optional(CONF_PATH): cv.templatable(cv.string_strict),
    # No default: the presence of the key is what distinguishes a file target from a raw one
    # (a default would fill it in and make that check meaningless).
    cv.Optional(CONF_FORMAT): cv.one_of("kv", "json", lower=True),
    cv.Optional(CONF_DEVICE): cv.use_id(RawStorage),
    # Not templatable on purpose: codegen computes each region's room from these addresses,
    # which a runtime lambda would hide.
    cv.Optional(CONF_ADDRESS): cv.hex_uint32_t,
    # Globals with restore_value. cv.use_id(cg.Component) because the globals
    # component declares several unrelated classes (GlobalsComponent,
    # RestoringGlobalsComponent, RestoringGlobalStringComponent) -- only the
    # id string is consumed here (baked into the name<->key table), the
    # variable itself is never awaited.
    cv.Optional(CONF_PREFERENCES): cv.ensure_list(cv.use_id(cg.Component)),
}


def _validate_preferences_target(config: ConfigType) -> ConfigType:
    # A backup action is in use -- AUTO_LOAD pulls in storage + binary_storage for the engine and
    # the NVSStore view it reads the live preferences through.
    _get_pref_data().backup_used = True
    has_path = CONF_PATH in config
    has_device = CONF_DEVICE in config
    if has_path == has_device:
        raise cv.Invalid("Exactly one of 'path' or 'device' is required")
    if has_device:
        if CONF_FORMAT in config:
            raise cv.Invalid(
                "'format' does not apply to a raw device: the blob is written as stored, "
                "there is nothing to render"
            )
        if CONF_ADDRESS not in config:
            raise cv.Invalid("'address' is required when the target is a raw device")
    elif CONF_ADDRESS in config:
        raise cv.Invalid("'address' only applies to a raw device target ('device:')")
    if config.get(CONF_FORMAT) == "json":
        # Tell the config-taking AUTO_LOAD that the json component is needed.
        _get_pref_data().json_required = True
    return config


_EXPORT_PREFERENCES_SCHEMA = cv.All(
    cv.only_on(["esp32"]),
    cv.Schema(_PREFERENCES_ACTION_BASE),
    _validate_preferences_target,
)

_IMPORT_PREFERENCES_SCHEMA = cv.All(
    cv.only_on(["esp32"]),
    cv.Schema(
        {
            **_PREFERENCES_ACTION_BASE,
            # Preferences are read at boot -- imported values only take effect
            # after a restart. Opt-in convenience.
            cv.Optional(CONF_REBOOT, default=False): cv.boolean,
        }
    ),
    _validate_preferences_target,
)


# Per-type version constants of EntityBase::make_entity_preference_() callers.
# Keep in sync: fan/fan.cpp, climate/climate.cpp; every other core entity uses
# the default version 0. template text is special-cased (trait-salted key).
# (module, class, version, EntityKind) -- kinds map to real-struct codecs in
# preferences_backup.cpp; anything not matched below registers as RAW (named,
# hex value). datetime template platforms carry their own versions.
# Container arithmetic, used to catch overlapping regions at config time. Layout is fixed by
# preferences_backup.cpp: a 16-byte header plus {key u32, len u16, blob} per entry.
_RAW_PREF_HEADER = 16
_RAW_PREF_ENTRY_OVERHEAD = 6
_PREF_TYPE_SIZES = {
    "BOOL": 1,
    "I8": 1,
    "U8": 1,
    "I16": 2,
    "U16": 2,
    "I32": 4,
    "U32": 4,
    "F32": 4,
    "F64": 8,
}


def _raw_pref_size(entries: list[tuple[str, int]]) -> int:
    """Exact container size for an explicit selection: (PrefType tag, count) per entry."""
    total = _RAW_PREF_HEADER
    for tag, count in entries:
        # STRING blobs are length-prefixed char[SZ], with SZ already carried in count.
        elem = 1 if tag == "STRING" else _PREF_TYPE_SIZES[tag]
        total += _RAW_PREF_ENTRY_OVERHEAD + elem * count
    return total


async def _resolve_raw_pref_regions() -> None:
    """Hands every raw preferences action the room it actually has, and rejects regions that
    would run into each other.

    Codegen knows every action's address, so nobody has to repeat "and it may use N bytes":
    a region reaches up to the next address on the same device, and the last one to the end of
    the device -- which only the device knows, hence window 0 for it. An export and its import
    share one address by design (that is the pair), so actions are grouped by address, not
    counted individually.

    Where a selection is explicit the container size is exact and a collision is a config
    error. An unrestricted selection grows with the app, so that case cannot be sized here and
    is caught at runtime by the window instead -- the export refuses rather than writing into
    the neighbouring region."""
    for device, actions in _get_pref_data().raw_pref_regions.items():
        by_address: dict[int, dict] = {}
        for action in actions:
            region = by_address.setdefault(
                action["address"], {"size": None, "actions": []}
            )
            region["actions"].append(action)
            if action["size"] is not None:
                region["size"] = max(region["size"] or 0, action["size"])

        addresses = sorted(by_address)
        for i, address in enumerate(addresses):
            region = by_address[address]
            if i + 1 < len(addresses):
                window = addresses[i + 1] - address
                size = region["size"]
                if size is not None and size > window:
                    raise cv.Invalid(
                        f"The preferences region at 0x{address:X} on '{device}' needs {size} "
                        f"bytes and would run into the region at 0x{addresses[i + 1]:X} "
                        f"({window} bytes apart)"
                    )
            else:
                window = 0  # to the end of the device
            for action in region["actions"]:
                cg.add(action["var"].set_raw_target(action["device"], address, window))


def _register_raw_pref_region(
    device_id: ID, device_var: cg.MockObj, address: int, size: int, var: cg.MockObj
) -> None:
    data = _get_pref_data()
    data.raw_pref_regions.setdefault(str(device_id), []).append(
        {"address": address, "size": size, "var": var, "device": device_var}
    )
    if not data.raw_pref_job_queued:
        data.raw_pref_job_queued = True
        # FINAL: every action must be built before the regions can be laid out.
        CORE.add_job(
            coroutine_with_priority(CoroPriority.FINAL)(_resolve_raw_pref_regions)
        )


async def _build_preferences_action(
    config: ConfigType, action_id: ID, template_arg: cg.TemplateArguments, args: list
):
    var = cg.new_Pvariable(action_id, template_arg)
    cg.add_define("USE_PREFERENCES_BACKUP")
    # The active preferences store the engine reads/writes through (esp32::get_preferences_store())
    # is a binary_storage NVSStore -- make sure that class is compiled and kept in the build even
    # when no `type: nvs` device or storage-backed preferences path pulls it in on its own.
    cg.add_define("USE_BINARY_STORAGE_NVS")
    CORE.data.setdefault("binary_storage_device_types", set()).add("nvs")
    # Once per build, not per action: naming is a property of the node, not of the action.
    data = _get_pref_data()
    if not data.sensor_pref_job_queued:
        data.sensor_pref_job_queued = True
        CORE.add_job(
            coroutine_with_priority(CoroPriority.FINAL)(_register_typed_sensors)
        )
        CORE.add_job(
            coroutine_with_priority(CoroPriority.FINAL)(_register_component_prefs)
        )
    if CONF_PATH in config:
        template_ = await cg.templatable(config[CONF_PATH], args, cg.std_string)
        cg.add(var.set_path(template_))
        cg.add(var.set_format(config.get(CONF_FORMAT, "kv")))

    def _bake(entries, restrict):
        # entity-only selections produce zero table entries: emitting
        # "static const T x[] = {}" would be a zero-size array (GNU
        # extension, not ISO C++) -- pass a null table instead
        if not entries:
            cg.add(var.set_selection(cg.nullptr, 0, restrict))
            return

        arr = f"{action_id}_psel"
        cg.add_global(
            cg.RawExpression(
                f"static const esphome::preferences::PrefSelection {arr}[] = {{"
                + ", ".join(entries)
                + "}"
            )
        )
        cg.add(var.set_selection(cg.RawExpression(arr), len(entries), restrict))

    def _entry(name, tag, count):
        key = _global_nvs_key(name)
        return f'{{"{name}", {key}UL, esphome::preferences::PrefType::{tag}, {count}}}'

    if selection := config.get(CONF_PREFERENCES):
        # get_variable_with_full_id is a coroutine: it suspends until the
        # global's own to_code has registered the variable -- the declaration
        # ID it returns carries the real C++ class (codegen-world data, no
        # validation-step leftovers).
        entries = []
        sizes = []
        has_entities = False
        for gid in selection:
            full_id, obj = await cg.get_variable_with_full_id(gid)
            parsed = _pref_type_from_class(str(full_id.type))
            if parsed is not None:
                entries.append(_entry(gid.id, *parsed))
                sizes.append(parsed)
                continue
            # anything else is treated as an entity: the runtime sweep
            # resolves name/kind/key from the live object; unresolvable
            # selections log a loud skip at play time
            cg.add(var.add_selected_entity(obj))
            has_entities = True
        if entries or has_entities:
            _bake(entries, True)
        # Entity selections carry no codegen-known blob size (their layout is a component
        # private, resolved by the runtime sweep) -- the size stays unknown then, and only the
        # window guards that case.
        raw_size = None if has_entities else _raw_pref_size(sizes)
    else:
        # All mode: enumerate the codegen variable registry once every
        # pending to_code has run. Scheduled as its own coroutine job -- it is
        # enqueued behind all already-queued component jobs, so the globals
        # are registered by the time it executes.
        # globals' own to_code runs at CoroPriority.LATE (-100) -- an
        # unprioritized job would enumerate CORE.variables BEFORE any global
        # is registered (verified empirically: 14 vars, zero globals).
        # FINAL (-1000) queues the bake after every component job.
        @coroutine_with_priority(CoroPriority.FINAL)
        async def _bake_all():
            # globals only -- entity naming is entirely the runtime sweep's job
            entries = []
            for reg_id in CORE.variables:
                parsed = _pref_type_from_class(str(reg_id.type))
                if parsed is not None:
                    entries.append(_entry(reg_id.id, *parsed))
            if entries:
                _bake(entries, False)

        CORE.add_job(_bake_all)
        raw_size = (
            None  # the namespace grows with the app -- only the window can guard this
        )

    if CONF_DEVICE in config:
        device_var = await cg.get_variable(config[CONF_DEVICE])
        _register_raw_pref_region(
            config[CONF_DEVICE], device_var, config[CONF_ADDRESS], raw_size, var
        )
    return var


@automation.register_action(
    "preferences.export",
    ExportPreferencesAction,
    _EXPORT_PREFERENCES_SCHEMA,
    synchronous=True,
)
async def export_preferences_to_code(
    config: ConfigType, action_id: ID, template_arg: cg.TemplateArguments, args: list
):
    return await _build_preferences_action(config, action_id, template_arg, args)


@automation.register_action(
    "preferences.import",
    ImportPreferencesAction,
    _IMPORT_PREFERENCES_SCHEMA,
    synchronous=True,
)
async def import_preferences_to_code(
    config: ConfigType, action_id: ID, template_arg: cg.TemplateArguments, args: list
):
    var = await _build_preferences_action(config, action_id, template_arg, args)
    cg.add(var.set_reboot(config[CONF_REBOOT]))
    return var
