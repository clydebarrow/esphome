import esphome.codegen as cg
from esphome.components import sensor
from esphome.components.ags10.sensor import CONF_RESISTANCE
from esphome.components.daly_bms.sensor import ICON_CURRENT_DC
from esphome.components.sicom import (
    CONF_SICOM_SENSOR_ID,
    SICOM_SENSOR_SCHEMA,
    sicom_devices,
)
import esphome.config_validation as cv
from esphome.const import (
    CONF_CURRENT,
    CONF_DEVICE,
    CONF_INDEX,
    CONF_TYPE,
    CONF_VOLTAGE,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_VOLTAGE,
    ICON_FLASH,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_OHM,
    UNIT_VOLT,
)

DEPENDENCIES = ["sicom"]
ICON_RESISTOR = "mdi:resistor"

CONFIG_SCHEMA = cv.typed_schema(
    {
        CONF_VOLTAGE: sensor.sensor_schema(
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_VOLT,
            icon=ICON_FLASH,
            accuracy_decimals=3,
        ).extend(SICOM_SENSOR_SCHEMA),
        CONF_RESISTANCE: sensor.sensor_schema(
            device_class=DEVICE_CLASS_RESISTANCE,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_OHM,
            icon=ICON_RESISTOR,
            accuracy_decimals=0,
        ).extend(SICOM_SENSOR_SCHEMA),
        CONF_CURRENT: sensor.sensor_schema(
            device_class=DEVICE_CLASS_CURRENT,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_AMPERE,
            icon=ICON_CURRENT_DC,
            accuracy_decimals=2,
        ).extend(SICOM_SENSOR_SCHEMA),
    }
)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_DEVICE])
    device = sicom_devices[config[CONF_DEVICE]]
    sensors = device.sensors[config[CONF_TYPE]]
    index = config[CONF_INDEX]
    if index >= len(sensors):
        raise cv.Invalid(
            f"Sensor index {index} is greater than the number of sensors ({device.sensor_count}) for device {device.name}"
        )
    stype = sensors[index]
    var = await sensor.new_sensor(config)
    sicom_sensor_var = cg.new_Pvariable(
        config[CONF_SICOM_SENSOR_ID], var, stype.offset, stype.data_type, stype.scale
    )
    cg.add(paren.add_sensor(sicom_sensor_var))
    return var
