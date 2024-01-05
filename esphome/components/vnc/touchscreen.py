import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

from esphome.components import touchscreen
from . import VNCDisplay, CONF_VNC_ID, vnc_ns

VNCTouchscreen = vnc_ns.class_("VNCTouchscreen", touchscreen.Touchscreen)


CONFIG_SCHEMA = touchscreen.TOUCHSCREEN_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(VNCTouchscreen),
        cv.GenerateID(CONF_VNC_ID): cv.use_id(VNCDisplay),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    vnc = await cg.get_variable(config[CONF_VNC_ID])
    cg.add(vnc.add_touchscreen(var))
    await touchscreen.register_touchscreen(var, config)
