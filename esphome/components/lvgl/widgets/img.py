import logging

import esphome.codegen as cg
from esphome.components.image import (
    INSTANCE_TYPE as IMAGE_TYPE,
    ImageMetaData,
    get_image_metadata,
    is_multiframe,
)
from esphome.components.mapping import get_mapping_metadata
import esphome.config_validation as cv
from esphome.const import (
    CONF_ANGLE,
    CONF_DURATION,
    CONF_ID,
    CONF_MODE,
    CONF_OFFSET_X,
    CONF_OFFSET_Y,
    CONF_ROTATION,
)
from esphome.core import ID

from .. import defines as df
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

_LOGGER = logging.getLogger(__name__)

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
# `ImgType._will_be_animated` below); otherwise unused. Mirrors `animimg:`'s own options
# so a static `image:` that starts pointing at an animated source "just works" without
# the user needing to switch to `animimg:` and re-declare frames as a list.
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

# Attribute name used to cache, directly on a Widget instance, whether it was created
# as an animimg -- see `to_code` below. Needed because the underlying LVGL object's
# class (lv_image vs lv_animimg) is fixed for good at creation time, so every update
# call after that has to keep using the creation-time answer, not re-derive one from
# that call's own (possibly different, or absent) `src:`.
_ANIMATED_ATTR = "_lvgl_image_is_animated"


def _src_is_multiframe(src: object) -> bool:
    """True if `src` is a plain id declared in a way that may produce multiple
    frames (currently: any `platform: animation` entry). False for a static image, a
    `!lambda`, or a `mapping:` entry -- there is no way to know a lambda/mapping's
    frame count, and only a plain id can be resolved at config-validation time, before
    any codegen (and thus any real decoded ImageMetaData) exists.
    """
    return isinstance(src, ID) and is_multiframe(src)


async def _get_real_metadata(src: object) -> ImageMetaData | None:
    """Resolve a plain image/animation id's real, decoded metadata (frame count,
    per-frame timing, ...). Only available once the id's own `to_code` has run, so
    only call this during codegen (never during validation).
    """
    if not isinstance(src, ID):
        return None
    await cg.get_variable(src)
    return get_image_metadata(src.id)


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

    def _future_update_is_multiframe(self, widget_id: ID) -> bool:
        """True if some `lvgl.image.update` elsewhere in the config points this
        widget's `src:` at a multi-frame image/animation.

        `df.get_updated_widgets()` is populated by the `add_extra` validator on every
        `lvgl.image.update` action's schema as it's validated -- the same validation
        pass that runs `CONFIG_SCHEMA` for this widget's own declaration -- so by the
        time any widget's `obj_creator`/`to_code` runs (codegen, a later stage), it
        already holds every update targeting every widget, regardless of where in the
        file they're declared.
        """
        for update_conf in df.get_updated_widgets().get(self, ()):
            if not _src_is_multiframe(update_conf.get(CONF_SRC)):
                continue
            if any(
                id_conf[CONF_ID] == widget_id
                for id_conf in update_conf.get(CONF_ID, ())
            ):
                return True
        return False

    def _will_be_animated(self, config: dict) -> bool:
        """Decide, from a widget's own declaration config, whether it should be
        created as an animimg. Only meaningful for a creation config (a `CONF_ID` that
        is this widget's own id, not an update action's list of target ids) -- the
        result is cached on the Widget once computed, see `to_code` below.
        """
        return _src_is_multiframe(
            config.get(CONF_SRC)
        ) or self._future_update_is_multiframe(config[CONF_ID])

    async def obj_creator(self, parent, config: dict):
        if self._will_be_animated(config):
            # Upstream LVGL's lv_animimg widget extends lv_image, so every other
            # property this widget sets (pivot, rotation, zoom, antialias, ...) still
            # applies; only the src/animation-timing setup differs, see to_code below.
            add_lv_use("animimg")
            return lv_expr.call("animimg_create", parent)
        return await super().obj_creator(parent, config)

    async def to_code(self, w: Widget, config):
        if not hasattr(w, _ANIMATED_ATTR):
            # First call: this is the widget's creation, on the very config
            # `obj_creator` just used to pick lv_image vs lv_animimg -- compute the
            # same answer once and cache it for every later `lvgl.image.update` call.
            setattr(w, _ANIMATED_ATTR, self._will_be_animated(config))
        if getattr(w, _ANIMATED_ATTR):
            await self._animimg_to_code(w, config)
        else:
            if CONF_DURATION in config:
                # Never animated (neither this declaration nor any update anywhere
                # targets it with a multi-frame src): duration only ever affects
                # animimg playback, so an explicit value here would silently do
                # nothing.
                _LOGGER.warning(
                    "'%s' has no effect on 'image' widget '%s': its 'src:' is not "
                    "a multi-frame image/animation",
                    CONF_DURATION,
                    w.var,
                )
            await w.set_property(CONF_SRC, await lv_image.process(config.get(CONF_SRC)))
        for prop, validator in BASE_IMG_SCHEMA.schema.items():
            await w.set_property(prop, config, processor=validator)

    @staticmethod
    async def _animimg_to_code(w: Widget, config: dict) -> None:
        src = config.get(CONF_SRC)
        if src is not None:
            lv.animimg_set_src(w.obj, await lv_image.process(src))
        if (repeat_count := config.get(CONF_REPEAT_COUNT)) is not None:
            lv.animimg_set_repeat_count(w.obj, repeat_count)
        duration = config.get(CONF_DURATION)
        if duration is None and src is not None:
            # No explicit duration, but src is (re)established this call: derive one
            # from the source's own timing (e.g. a GIF's per-frame delays), falling
            # back to a flat 100ms per frame. A call that doesn't touch src leaves
            # whatever duration is already set alone, same as animimg: itself.
            metadata = await _get_real_metadata(src)
            if metadata is not None:
                duration = metadata.animation_duration_ms or (
                    100 * metadata.frame_count
                )
        if duration is not None:
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
