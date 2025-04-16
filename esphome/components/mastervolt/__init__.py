import esphome.codegen as cg
from esphome.components.canbus import CONF_CAN_ID, CONF_CANBUS_ID, CanbusComponent
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@clydebarrow"]

DOMAIN = "mastervolt"

DEPENDENCIES = ["canbus"]

AUTO_LOAD = ["bytebuffer", "sensor"]

CONF_DEVICES = "devices"
CONF_DEVICE_ID = "device_id"

mastervolt_ns = cg.esphome_ns.namespace("mastervolt")
Mastervolt = mastervolt_ns.class_("Mastervolt", cg.Component)
MastervoltDevice = mastervolt_ns.class_("MastervoltDevice")
MastervoltSensor = mastervolt_ns.class_("MastervoltSensor")


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Mastervolt),
        cv.GenerateID(CONF_CANBUS_ID): cv.use_id(CanbusComponent),
        cv.Required(CONF_DEVICES): cv.ensure_list(
            cv.Schema(
                {
                    cv.GenerateID(): cv.declare_id(MastervoltDevice),
                    cv.Required(CONF_CAN_ID): cv.int_range(min=0, max=0x1FFFFFFF),
                }
            )
        ),
    }
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

    for device_config in config[CONF_DEVICES]:
        device = cg.new_Pvariable(device_config[CONF_ID], device_config[CONF_CAN_ID])
        cg.add(var.add_device(device))
