"""LVGL widget for the skyecho component.

Registers a custom ``skyecho_traffic`` LVGL widget that renders GDL90 traffic
targets around a centred ownship symbol. The widget is a compound widget: a
plain ``lv_obj`` container populated at runtime by the C++ ``TrafficWidget``
class. The ownship and target symbols are bundled with the component (see the
``images`` directory) and embedded through the standard ``image`` pipeline.
"""

from pathlib import Path

import esphome.codegen as cg
from esphome.components.const import CONF_BYTE_ORDER
from esphome.components.file.image import new_image
from esphome.components.image import CONF_INVERT_ALPHA, CONF_TRANSPARENCY, Image_
import esphome.config_validation as cv
from esphome.const import (
    CONF_DITHER,
    CONF_FILE,
    CONF_ID,
    CONF_RANGE,
    CONF_RAW_DATA_ID,
    CONF_RESIZE,
    CONF_TYPE,
)
from esphome.core import CORE, ID
from esphome.cpp_generator import MockObj

from ..lvgl.defines import CONF_IMAGE, CONF_MAIN, get_lv_images_used
from ..lvgl.lvcode import lv_add
from ..lvgl.types import LvCompound, LvType
from ..lvgl.widgets import Widget, WidgetType
from ..lvgl.widgets.label import CONF_LABEL
from . import CONF_SKYECHO_ID, DOMAIN, KEY_NEEDS_IMAGES, SkyEchoComponent

CONF_SKYECHO_TRAFFIC = "skyecho_traffic"
CONF_MAX_TARGETS = "max_targets"
CONF_RANGE_RINGS = "range_rings"
CONF_OWNSHIP_SIZE = "ownship_size"
CONF_TARGET_SIZE = "target_size"

# Must not exceed MAX_TRAFFIC_TRACKED in skyecho.h.
MAX_TARGETS = 20

IMAGES_DIR = Path(__file__).parent / "images"

TrafficWidgetType = LvType("esphome::skyecho::TrafficWidget", parents=(LvCompound,))

TRAFFIC_SCHEMA = {
    cv.GenerateID(CONF_SKYECHO_ID): cv.use_id(SkyEchoComponent),
    cv.Optional(CONF_RANGE, default="10000m"): cv.All(
        cv.distance, cv.float_range(min=100.0)
    ),
    cv.Optional(CONF_MAX_TARGETS, default=8): cv.int_range(min=1, max=MAX_TARGETS),
    cv.Optional(CONF_RANGE_RINGS, default=3): cv.int_range(min=0, max=6),
    cv.Optional(CONF_OWNSHIP_SIZE, default=20): cv.int_range(min=1, max=200),
    cv.Optional(CONF_TARGET_SIZE, default=20): cv.int_range(min=1, max=200),
}


async def _bundle_image(
    widget_id: str, name: str, filename: str, size: int | None = None
) -> MockObj:
    """Embed one of the component's bundled images and return its instance.

    SVG sources are rendered at ``size`` pixels; a size of ``None`` keeps the
    image's native dimensions.
    """
    image_id = ID(f"{widget_id}_{name}_image", is_declaration=True, type=Image_)
    config = {
        CONF_ID: image_id,
        CONF_RAW_DATA_ID: ID(
            f"{widget_id}_{name}_data", is_declaration=True, type=cg.uint8
        ),
        CONF_FILE: str(IMAGES_DIR / filename),
        CONF_TYPE: "RGB565",
        CONF_TRANSPARENCY: "alpha_channel",
        CONF_DITHER: "NONE",
        CONF_INVERT_ALPHA: False,
        CONF_BYTE_ORDER: "LITTLE_ENDIAN",
    }
    if size is not None:
        config[CONF_RESIZE] = (size, size)
    var = await new_image(config)
    # Register with LVGL so it enables software support for the image's colour
    # format (RGB565 with alpha -> RGB565A8), otherwise it cannot be drawn.
    get_lv_images_used().add(image_id)
    return var


class SkyEchoTrafficWidgetType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_SKYECHO_TRAFFIC,
            TrafficWidgetType,
            (CONF_MAIN,),
            schema=TRAFFIC_SCHEMA,
            # The base object is a plain container; the C++ class fills it in.
            lv_name="obj",
        )

    def validate(self, value):
        # Flag that the bundled images are needed so skyecho's AUTO_LOAD pulls in
        # the image/file components (which depend on display).
        CORE.data.setdefault(DOMAIN, {})[KEY_NEEDS_IMAGES] = True
        return value

    def get_uses(self):
        return CONF_IMAGE, CONF_LABEL

    async def to_code(self, w: Widget, config: dict):
        parent = await cg.get_variable(config[CONF_SKYECHO_ID])
        lv_add(w.var.set_parent(parent))
        lv_add(w.var.set_range(config[CONF_RANGE]))
        lv_add(w.var.set_max_targets(config[CONF_MAX_TARGETS]))
        lv_add(w.var.set_range_rings(config[CONF_RANGE_RINGS]))

        widget_id = config[CONF_ID].id
        ownship_image = await _bundle_image(
            widget_id, "ownship", "ownship.svg", config[CONF_OWNSHIP_SIZE]
        )
        target_image = await _bundle_image(
            widget_id, "target", "target.svg", config[CONF_TARGET_SIZE]
        )
        north_image = await _bundle_image(widget_id, "north", "north.png")
        lv_add(w.var.set_ownship_image(ownship_image))
        lv_add(w.var.set_target_image(target_image))
        lv_add(w.var.set_north_image(north_image))

        # Build the child objects once all setters have run.
        lv_add(w.var.build())


skyecho_traffic_spec = SkyEchoTrafficWidgetType()
