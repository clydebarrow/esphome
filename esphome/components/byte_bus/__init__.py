import types
import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv

from esphome.schema_extractors import schema_extractor_registry

from esphome.const import (
    CONF_ID,
)

CODEOWNERS = ["@clydebarrow", "@nielsnl68"]

CONF_BUS_TYPE = "bus_type"

byte_bus_ns = cg.esphome_ns.namespace("byte_bus")
ByteBus = byte_bus_ns.class_("ByteBus")


class DatabusEntry:
    def __init__(self, name, fun, type_id, schema, final=None, final_args=None):
        self.name = name
        self.fun = fun
        self.type_id = type_id
        self.raw_schema = schema
        self.final = final
        self.final_args = final_args

    @property
    def coroutine_fun(self):
        from esphome.core import coroutine

        return coroutine(self.fun)

    @property
    def schema(self):
        from esphome.config_validation import Schema

        return Schema(self.raw_schema)


class Registry(dict[str, DatabusEntry]):
    def __init__(self, base_schema=None, type_id_key=None):
        super().__init__()
        self.base_schema = base_schema or {}
        self.type_id_key = type_id_key

    def register(self, name, type_id, schema, final=None, final_args=None):
        def decorator(fun):
            self[name] = DatabusEntry(name, fun, type_id, schema, final, final_args)
            return fun

        return decorator


DATABUS_REGISTRY = Registry()


def include_databus(name, *, bus_class, schema, final_validate=None, final_args=None):
    result = DATABUS_REGISTRY.register(
        name, bus_class, schema, final_validate, final_args
    )
    return result


def validate_databus_registry(base_schema, **kwargs):
    default_schema_option = kwargs.pop("default", None)
    base_schema = cv.ensure_schema(base_schema).extend(
        {
            cv.Optional(CONF_BUS_TYPE): cv.valid,
        },
        extra=cv.ALLOW_EXTRA,
    )

    @schema_extractor_registry(DATABUS_REGISTRY)
    def validator(value):

        default = (
            default_schema_option
            if not callable(default_schema_option)
            else default_schema_option(value)
        )

        if not isinstance(value, dict):
            raise cv.Invalid("This value must be dict !!")
        value = value.copy()

        key = value.pop(CONF_BUS_TYPE, default)
        if key is None:
            raise cv.Invalid(f"{CONF_BUS_TYPE} not specified!")

        models = cv.extract_keys(DATABUS_REGISTRY)
        key_validator = cv.one_of(*models, **kwargs)
        key_v = key_validator(key)

        if not isinstance(base_schema, cv.Schema):
            raise cv.Invalid("base_schema must be a schema !!")

        new_schema = base_schema.extend(DATABUS_REGISTRY[key_v].raw_schema)

        value = new_schema(value)
        value[CONF_BUS_TYPE] = key_v
        return value

    return validator


async def register_databus(config):
    databus = DATABUS_REGISTRY[config[CONF_BUS_TYPE]]
    rhs = databus.type_id.new()
    bus = config[CONF_ID].copy()
    bus.id = bus.id + "_bus"
    var = cg.Pvariable(bus, rhs, databus.type_id)
    databus.bus_id = bus.id
    return await databus.fun(config, var, databus)


def get_config_from_id(value):
    fconf = fv.full_config.get()
    path = fconf.get_path_for_id(value)[:-1]
    return fconf.get_config_for_path(path)


def final_validate_databus_schema(name: str):
    def validate(config):
        databus = DATABUS_REGISTRY[config[CONF_BUS_TYPE]]

        if isinstance(databus.final, types.FunctionType):
            kargs = {} if databus.final_args is None else databus.final_args
            kargs["config"] = config
            return databus.final(name, **kargs)
        return config

    return validate
