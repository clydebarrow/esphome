import math

import esphome.codegen as cg
from esphome.components.sensor import Sensor, new_sensor, sensor_schema
import esphome.config_validation as cv
from esphome.const import (
    CONF_NAME,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY_STORAGE,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ENTITY_CATEGORY_NONE,
    ICON_BATTERY,
    ICON_CURRENT_AC,
    ICON_FLASH,
    ICON_POWER,
    ICON_THERMOMETER,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_HERTZ,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
)
from esphome.core import CORE

from ...const import ICON_CURRENT_DC
from ...daly_bms.sensor import UNIT_AMPERE_HOUR
from .. import (
    COMMAND_SENSOR_DATA,
    COMMAND_SETTINGS_DATA,
    CONF_CONFIGURE_ALL,
    CONF_GOODWE_ID,
    DATA_OFFSET,
    DOMAIN,
    KEY_MSGS,
    Goodwe,
    Parameter,
    goodwe_ns,
)

SensorParameter = goodwe_ns.class_("SensorParameter", Parameter, Sensor)
BatteryCurrentParameter = goodwe_ns.class_(
    "BatteryCurrentParameter", SensorParameter, Parameter
)


class GoodweSensor:
    def __init__(
        self,
        id,
        msgcode,
        offset,
        scale,
        datatype,
        unit_of_measurement,
        device_class,
        icon,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_NONE,
    ):
        self.id = id
        self.msgcode = msgcode
        self.offset = offset
        self.scale = scale
        self.datatype = datatype
        self.unit_of_measurement = unit_of_measurement
        self.device_class = device_class
        self.icon = icon
        self.state_class = state_class
        self.entity_category = entity_category

    def get_class(self):
        return SensorParameter.template(
            self.datatype, self.offset + DATA_OFFSET, self.scale
        )

    def get_name(self):
        return self.id.replace("_", " ").title()

    def get_schema(self):
        def validator(value):
            # Add message code to set for parent to process
            msgs = CORE.data.setdefault(DOMAIN, {}).setdefault(KEY_MSGS, set())
            msgs.add(self.msgcode)
            return cv.maybe_simple_value(
                sensor_schema(
                    self.get_class(),
                    unit_of_measurement=self.unit_of_measurement,
                    device_class=self.device_class,
                    state_class=self.state_class,
                    entity_category=self.entity_category,
                    icon=self.icon,
                    accuracy_decimals=int(math.log10(1.0 / self.scale)),
                ),
                key=CONF_NAME,
            )(value)

        return validator


class SettingSensor(GoodweSensor):
    def __init__(self, id, offset, scale=1.0, unit_of_measurement=cv.UNDEFINED):
        super().__init__(
            id,
            COMMAND_SETTINGS_DATA,
            offset,
            scale,
            cg.int16,
            unit_of_measurement,
            cv.UNDEFINED,
            ICON_BATTERY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        )


class VoltageSensor(GoodweSensor):
    def __init__(self, id, msgcode, offset):
        super().__init__(
            id,
            msgcode,
            offset,
            0.1,
            cg.uint16,
            UNIT_VOLT,
            DEVICE_CLASS_VOLTAGE,
            ICON_FLASH,
        )


class CurrentSensor(GoodweSensor):
    def __init__(self, id, msgcode, offset, dc=True):
        super().__init__(
            id,
            msgcode,
            offset,
            0.1,
            cg.uint16,
            UNIT_AMPERE,
            DEVICE_CLASS_CURRENT,
            ICON_CURRENT_DC if dc else ICON_CURRENT_AC,
        )


class BatteryCurrentSensor(CurrentSensor):
    def __init__(self, id, msgcode, offset, sign_offset):
        super().__init__(id, msgcode, offset)
        self.sign_offset = sign_offset

    def get_class(self):
        return BatteryCurrentParameter.template(
            self.offset + DATA_OFFSET, self.sign_offset + DATA_OFFSET, self.scale
        )


class TemperatureSensor(GoodweSensor):
    def __init__(self, id, msgcode, offset):
        super().__init__(
            id,
            msgcode,
            offset,
            0.1,
            cg.int16,
            UNIT_CELSIUS,
            DEVICE_CLASS_TEMPERATURE,
            ICON_THERMOMETER,
        )


class PercentageSensor(GoodweSensor):
    def __init__(self, id, msgcode, offset):
        super().__init__(
            id,
            msgcode,
            offset,
            1.0,
            cg.uint8,
            "%",
            DEVICE_CLASS_ENERGY_STORAGE,
            ICON_BATTERY,
        )


class PowerSensor(GoodweSensor):
    def __init__(self, id, msgcode, offset):
        super().__init__(
            id,
            msgcode,
            offset,
            1.0,
            cg.uint16,
            UNIT_WATT,
            DEVICE_CLASS_POWER,
            ICON_POWER,
        )


class IntegerSensor(GoodweSensor):
    def __init__(
        self,
        id,
        msgcode,
        offset,
        unit_of_measurement=None,
        device_class=cv.UNDEFINED,
        icon=ICON_BATTERY,
        datatype=cg.int16,
    ):
        super().__init__(
            id,
            msgcode,
            offset,
            1.0,
            datatype,
            unit_of_measurement=unit_of_measurement,
            device_class=device_class,
            icon=icon,
        )


SENSORS = [
    VoltageSensor("pv_voltage_1", COMMAND_SENSOR_DATA, 0),
    CurrentSensor("pv_current_1", COMMAND_SENSOR_DATA, 2),
    VoltageSensor("pv_voltage_2", COMMAND_SENSOR_DATA, 5),
    CurrentSensor("pv_current_2", COMMAND_SENSOR_DATA, 7),
    TemperatureSensor("battery_temperature", COMMAND_SENSOR_DATA, 16),
    VoltageSensor("battery_voltage", COMMAND_SENSOR_DATA, 10),
    BatteryCurrentSensor("battery_current", COMMAND_SENSOR_DATA, 18, 30),
    VoltageSensor("grid_voltage", COMMAND_SENSOR_DATA, 34),
    CurrentSensor("grid_current", COMMAND_SENSOR_DATA, 36, dc=False),
    PercentageSensor("battery_charge_state", COMMAND_SENSOR_DATA, 26),
    PercentageSensor("battery_health", COMMAND_SENSOR_DATA, 29),
    IntegerSensor(
        "battery_charge_limit",
        COMMAND_SENSOR_DATA,
        20,
        unit_of_measurement=UNIT_AMPERE,
        device_class=DEVICE_CLASS_CURRENT,
    ),
    IntegerSensor(
        "battery_discharge_limit",
        COMMAND_SENSOR_DATA,
        22,
        unit_of_measurement=UNIT_AMPERE,
        device_class=DEVICE_CLASS_CURRENT,
    ),
    GoodweSensor(
        "grid_frequency",
        COMMAND_SENSOR_DATA,
        40,
        0.01,
        cg.uint16,
        unit_of_measurement=UNIT_HERTZ,
        device_class=DEVICE_CLASS_FREQUENCY,
        icon=ICON_CURRENT_AC,
    ),
    VoltageSensor("backup_voltage", COMMAND_SENSOR_DATA, 43),
    CurrentSensor("backup_current", COMMAND_SENSOR_DATA, 45, dc=False),
    PowerSensor("backup_power", COMMAND_SENSOR_DATA, 81),
    PowerSensor("on_grid_power", COMMAND_SENSOR_DATA, 47),
    PowerSensor("total_power", COMMAND_SENSOR_DATA, 75),
    IntegerSensor(
        "grid_export_limit",
        COMMAND_SETTINGS_DATA,
        52,
        UNIT_WATT,
        device_class=DEVICE_CLASS_POWER,
    ),
    # Settings
    SettingSensor("capacity", 22, unit_of_measurement=UNIT_AMPERE_HOUR),
    SettingSensor("charge_voltage", 24, scale=0.1, unit_of_measurement=UNIT_VOLT),
    SettingSensor("charge_current", 26, unit_of_measurement=UNIT_AMPERE),
    SettingSensor("discharge_current", 28, unit_of_measurement=UNIT_AMPERE),
    SettingSensor("discharge_voltage", 30, scale=0.1, unit_of_measurement=UNIT_VOLT),
    SettingSensor("discharge_limit", 32, unit_of_measurement=UNIT_PERCENT),
    SettingSensor("bp_bms_protocol", 40),
    SettingSensor("power_factor", 42),
    SettingSensor("battery_soc_protection", 56),
    SettingSensor("grid_quality_check", 68),
]

CONFIG_SCHEMA = cv.Any(
    {
        cv.Required(CONF_CONFIGURE_ALL): True,
        **{
            cv.Optional(s.id, default={CONF_NAME: s.get_name()}): s.get_schema()
            for s in SENSORS
        },
        cv.GenerateID(CONF_GOODWE_ID): cv.use_id(Goodwe),
    },
    {
        cv.Optional(CONF_CONFIGURE_ALL, default=False): False,
        **{cv.Optional(s.id): s.get_schema() for s in SENSORS},
        cv.GenerateID(CONF_GOODWE_ID): cv.use_id(Goodwe),
    },
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_GOODWE_ID])
    for sensor in SENSORS:
        if sensor.id in config:
            var = await new_sensor(config[sensor.id], sensor.id)
            cg.add(parent.add_parameter(sensor.msgcode, var))
