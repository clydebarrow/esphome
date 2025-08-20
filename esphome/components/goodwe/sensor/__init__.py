import math

import esphome.codegen as cg
from esphome.components.sensor import new_sensor, sensor_schema
import esphome.config_validation as cv
from esphome.const import (
    CONF_NAME,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY_STORAGE,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
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
    UNIT_VOLT,
    UNIT_WATT,
)

from ...const import ICON_CURRENT_DC
from .. import (
    CONF_CONFIGURE_ALL,
    CONF_GOODWE_ID,
    DATA_OFFSET,
    Goodwe,
    Parameter,
    goodwe_ns,
)

SensorParameter = goodwe_ns.class_("SensorParameter", Parameter)
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
    ):
        self.id = id
        self.msgcode = msgcode
        self.offset = offset
        self.scale = scale
        self.datatype = datatype
        self.unit_of_measurement = unit_of_measurement
        self.device_class = device_class
        self.icon = icon

    def get_class(self):
        return SensorParameter.template(
            self.datatype, self.offset + DATA_OFFSET, self.scale
        )

    def get_name(self):
        return self.id.replace("_", " ").title()

    def get_schema(self):
        return cv.maybe_simple_value(
            sensor_schema(
                self.get_class(),
                unit_of_measurement=self.unit_of_measurement,
                device_class=self.device_class,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_NONE,
                icon=self.icon,
                accuracy_decimals=int(math.log10(1.0 / self.scale)),
            ),
            key=CONF_NAME,
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
        device_class=None,
        icon=ICON_BATTERY,
    ):
        super().__init__(
            id,
            msgcode,
            offset,
            1.0,
            cg.int16,
            unit_of_measurement=unit_of_measurement,
            device_class=device_class,
            icon=icon,
        )


SENSORS = [
    VoltageSensor("pv_voltage_1", 0x106, 0),
    CurrentSensor("pv_current_1", 0x106, 2),
    VoltageSensor("pv_voltage_2", 0x106, 5),
    CurrentSensor("pv_current_2", 0x106, 7),
    TemperatureSensor("battery_temperature", 0x106, 16),
    VoltageSensor("battery_voltage", 0x106, 10),
    BatteryCurrentSensor("battery_current", 0x106, 18, 30),
    VoltageSensor("grid_voltage", 0x106, 34),
    CurrentSensor("grid_current", 0x106, 36, dc=False),
    PercentageSensor("battery_charge_state", 0x106, 26),
    PercentageSensor("battery_health", 0x106, 29),
    IntegerSensor(
        "battery_charge_limit",
        0x106,
        20,
        unit_of_measurement=UNIT_AMPERE,
        device_class=DEVICE_CLASS_CURRENT,
    ),
    IntegerSensor(
        "battery_discharge_limit",
        0x106,
        22,
        unit_of_measurement=UNIT_AMPERE,
        device_class=DEVICE_CLASS_CURRENT,
    ),
    GoodweSensor(
        "grid_frequency",
        0x106,
        40,
        0.01,
        cg.uint16,
        unit_of_measurement=UNIT_HERTZ,
        device_class=DEVICE_CLASS_FREQUENCY,
        icon=ICON_CURRENT_AC,
    ),
    VoltageSensor("backup_voltage", 0x106, 43),
    CurrentSensor("backup_current", 0x106, 45, dc=False),
    PowerSensor("backup_power", 0x106, 81),
    PowerSensor("on_grid_power", 0x106, 47),
    PowerSensor("total_power", 0x106, 75),
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
