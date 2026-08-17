import esphome.codegen as cg
from esphome.components.image import (
    INSTANCE_TYPE as IMAGE_TYPE,
    ImageMetaData,
    get_image_metadata,
)
from esphome.components.mapping import get_mapping_metadata
import esphome.config_validation as cv
from esphome.const import (
    CONF_ANGLE,
    CONF_DURATION,
    CONF_MODE,
    CONF_OFFSET_X,
    CONF_OFFSET_Y,
    CONF_ROTATION,
)
from esphome.core import ID

from ..defines import (
    CONF_ANTIALIAS,
    CONF_AUTO_START,
    CONF_IMAGE,
    CONF_MAIN,
    CONF_MAPPING,
    CONF_PIVOT_X,
    CONF_PIVOT_Y,
    CONF_REPEAT_COUNT,
    CONF_SCALE,
    CONF_SRC,
    CONF_ZOOM,
    add_lv_use,
)
from ..lv_validation import (
    lv_angle,
    lv_bool,
    lv_image,
    lv_milliseconds,
    lv_repeat_count,
    scale,
    size,
)
from ..lvcode import lv, lv_expr
from ..types import lv_image_t
from . import Widget, WidgetType
from .label import CONF_LABEL

BASE_IMG_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PIVOT_X): size,
        cv.Optional(CONF_PIVOT_Y): size,
        cv.Exclusive(CONF_ANGLE, CONF_ROTATION): lv_angle,
        cv.Exclusive(CONF_ROTATION, CONF_ROTATION): lv_angle,
        cv.Exclusive(CONF_ZOOM, CONF_SCALE): scale,
        cv.Exclusive(CONF_SCALE, CONF_SCALE): scale,
        cv.Optional(CONF_OFFSET_X): size,
        cv.Optional(CONF_OFFSET_Y): size,
        cv.Optional(CONF_ANTIALIAS): lv_bool,
        cv.Optional(CONF_MODE): cv.invalid(f"{CONF_MODE} is not supported in LVGL 9.x"),
    }
)

# Only meaningful when `src:` turns out to reference a multi-frame image/animation (see
# `_detect_animated_src` below); otherwise unused. Mirrors `animimg:`'s own options so a
# static `image:` that starts pointing at an animated source "just works" without the
# user needing to switch to `animimg:` and re-declare frames as a list.
_ANIM_OPTIONS_SCHEMA = {
    cv.Optional(CONF_DURATION): lv_milliseconds,
    cv.Optional(CONF_REPEAT_COUNT, default="forever"): lv_repeat_count,
    cv.Optional(CONF_AUTO_START, default=True): cv.boolean,
}

IMG_SCHEMA = BASE_IMG_SCHEMA.extend(
    {
        cv.Required(CONF_SRC): lv_image,
        **_ANIM_OPTIONS_SCHEMA,
    }
)

IMG_MODIFY_SCHEMA = BASE_IMG_SCHEMA.extend(
    {
        cv.Optional(CONF_SRC): lv_image,
        **_ANIM_OPTIONS_SCHEMA,
    }
)

# Attribute name used to cache the animated-source decision directly on a Widget
# instance -- see `_get_animated_metadata` below. A module-level dict keyed by the
# widget's own id would not work here: an `lvgl.image.update` action's config carries
# its *target* id(s) as a list (it can update several widgets at once), not a single
# id usable as a stable per-widget key.
_ANIMIMG_METADATA_ATTR = "_lvgl_animimg_metadata"


async def _get_animated_metadata(
    w: Widget | None, config: dict
) -> ImageMetaData | None:
    """Resolve whether `src:` references a multi-frame image/animation.

    The underlying LVGL object's class (lv_image vs lv_animimg) is fixed for good at
    creation time, so once `w` exists, the first answer computed for it (from
    `obj_creator`'s creation-time config, via `to_code`'s first call) is cached on `w`
    and every later call -- e.g. from `lvgl.image.update` -- reuses it instead of
    re-deriving it from that call's own (possibly different, or absent) `src:`.

    Returns None (treat as static) for a `!lambda` or `mapping:` source, since there is
    no way to know its frame count before code generation runs, and for a plain id whose
    image turns out to have only one frame.
    """
    if w is not None and hasattr(w, _ANIMIMG_METADATA_ATTR):
        return getattr(w, _ANIMIMG_METADATA_ATTR)
    metadata = None
    src = config.get(CONF_SRC)
    if isinstance(src, ID):
        # Wait for the referenced image's own `to_code` (and its `add_metadata()` call)
        # to have run before consulting the metadata registry.
        await cg.get_variable(src)
        found = get_image_metadata(src.id)
        if found is not None and found.frame_count > 1:
            metadata = found
    if w is not None:
        setattr(w, _ANIMIMG_METADATA_ATTR, metadata)
    return metadata


class ImgType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_IMAGE,
            lv_image_t,
            (CONF_MAIN,),
            IMG_SCHEMA,
            IMG_MODIFY_SCHEMA,
        )

    def get_uses(self):
        return CONF_IMAGE, CONF_LABEL

    async def obj_creator(self, parent, config: dict):
        # `w` doesn't exist yet to cache onto; `to_code`'s first call (right after
        # creation, on this same `config`) will compute the same answer and cache it.
        metadata = await _get_animated_metadata(None, config)
        if metadata is not None:
            # Upstream LVGL's lv_animimg widget extends lv_image, so every other
            # property this widget sets (pivot, rotation, zoom, antialias, ...) still
            # applies; only the src/animation-timing setup differs, see to_code below.
            add_lv_use("animimg")
            return lv_expr.call("animimg_create", parent)
        return await super().obj_creator(parent, config)

    async def to_code(self, w: Widget, config):
        metadata = await _get_animated_metadata(w, config)
        if metadata is not None:
            await self._animimg_to_code(w, config, metadata)
        else:
            await w.set_property(CONF_SRC, await lv_image.process(config.get(CONF_SRC)))
        for prop, validator in BASE_IMG_SCHEMA.schema.items():
            await w.set_property(prop, config, processor=validator)

    @staticmethod
    async def _animimg_to_code(
        w: Widget, config: dict, metadata: ImageMetaData
    ) -> None:
        if src := config.get(CONF_SRC):
            lv.animimg_set_src(w.obj, await lv_image.process(src))
        if (repeat_count := config.get(CONF_REPEAT_COUNT)) is not None:
            lv.animimg_set_repeat_count(w.obj, repeat_count)
        duration = config.get(CONF_DURATION)
        if duration is None:
            # No explicit duration: derive one from the source's own timing (e.g. a
            # GIF's per-frame delays), falling back to a flat 100ms per frame.
            duration = metadata.animation_duration_ms or (100 * metadata.frame_count)
        lv.animimg_set_duration(w.obj, duration)
        if config.get(CONF_AUTO_START):
            lv.animimg_start(w.obj)

    def final_validate(self, widget, update_config, widget_config, path):
        src = update_config.get(CONF_SRC)
        if isinstance(src, dict) and CONF_MAPPING in src:
            mapping_id = src[CONF_MAPPING]
            metadata = get_mapping_metadata(mapping_id.id)
            if str(metadata.to_.data_type) != str(IMAGE_TYPE):
                raise cv.Invalid(
                    f"Mapping '{mapping_id}' does not map to an image type, but '{metadata.to_.data_type}'",
                    path=path + [CONF_SRC, CONF_MAPPING],
                )


img_spec = ImgType()
