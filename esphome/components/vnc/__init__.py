import esphome.codegen as cg
from esphome.components import display

CODEOWNERS = ["@clydebarrow"]


vnc_ns = cg.esphome_ns.namespace("vnc")
VNCDisplay = vnc_ns.class_(
    "VNCDisplay",
    cg.Component,
    display.Display,
)


CONF_VNC_ID = "vnc_id"
