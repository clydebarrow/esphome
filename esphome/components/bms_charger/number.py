import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    DEVICE_CLASS_CURRENT,
    ENTITY_CATEGORY_CONFIG,
    UNIT_AMPERE,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_INITIAL_VALUE,
    CONF_RESTORE_VALUE,
    UNIT_VOLT,
    DEVICE_CLASS_VOLTAGE,
)
from . import ParamNumber, CONF_CHARGER_ID, ChargerComponent

CONF_MAX_CHARGE_CURRENT = "max_charge_current"
CONF_MAX_DISCHARGE_CURRENT = "max_discharge_current"
CONF_MAX_CHARGE_VOLTAGE = "max_charge_voltage"
ICON_CURRENT_DC = "mdi:current-dc"


def validate_min_max(config):
    if config[CONF_MAX_VALUE] <= config[CONF_MIN_VALUE]:
        raise cv.Invalid("max_value must be greater than min_value")
    if CONF_INITIAL_VALUE not in config:
        config[CONF_INITIAL_VALUE] = config[CONF_MAX_VALUE]
    if (
        not config[CONF_MIN_VALUE]
        <= config[CONF_INITIAL_VALUE]
        <= config[CONF_MAX_VALUE]
    ):
        raise cv.Invalid("initial_value must be between min_value and max_value")
    return config


CURRENT_SCHEMA = cv.All(
    number.number_schema(
        ParamNumber,
        unit_of_measurement=UNIT_AMPERE,
        device_class=DEVICE_CLASS_CURRENT,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_CURRENT_DC,
    ).extend(
        {
            cv.Optional(CONF_MAX_VALUE, default=100): cv.int_range(min=0, max=10000),
            cv.Optional(CONF_MIN_VALUE, default=0): cv.int_range(min=0, max=10000),
            cv.Optional(CONF_INITIAL_VALUE): cv.float_,
            cv.Optional(CONF_RESTORE_VALUE, default=False): cv.boolean,
        }
    ),
    validate_min_max,
)

VOLTAGE_SCHEMA = cv.All(
    number.number_schema(
        ParamNumber,
        unit_of_measurement=UNIT_VOLT,
        device_class=DEVICE_CLASS_VOLTAGE,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_CURRENT_DC,
    ).extend(
        {
            cv.Optional(CONF_MAX_VALUE, default=56): cv.int_range(min=0, max=1000),
            cv.Optional(CONF_MIN_VALUE, default=48): cv.int_range(min=0, max=1000),
            cv.Optional(CONF_INITIAL_VALUE): cv.float_,
            cv.Optional(CONF_RESTORE_VALUE, default=False): cv.boolean,
        }
    ),
    validate_min_max,
)

NUMBERS = (CONF_MAX_DISCHARGE_CURRENT, CONF_MAX_CHARGE_CURRENT, CONF_MAX_CHARGE_VOLTAGE)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_CHARGER_ID): cv.use_id(ChargerComponent),
        cv.Optional(CONF_MAX_CHARGE_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_MAX_DISCHARGE_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_MAX_CHARGE_VOLTAGE): VOLTAGE_SCHEMA,
    },
)


async def to_code(config):
    charger = config[CONF_CHARGER_ID]
    charger_component = await cg.get_variable(charger)
    for conf_key in NUMBERS:
        if entry := config.get(conf_key):
            num = await number.new_number(
                entry,
                min_value=entry[CONF_MIN_VALUE],
                max_value=entry[CONF_MAX_VALUE],
                step=1,
            )

            await cg.register_component(num, entry)
            await cg.register_parented(num, charger)
            cg.add(num.set_initial_value(entry[CONF_INITIAL_VALUE]))
            cg.add(num.set_restore_value(entry[CONF_RESTORE_VALUE]))
            cg.add(
                cg.RawExpression(f"{charger_component}->set_{conf_key}_number({num});")
            )
