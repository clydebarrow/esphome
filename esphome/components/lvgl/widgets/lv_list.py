from esphome import automation
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_INDEX, CONF_TEXT
from esphome.cpp_types import nullptr

from ..automation import action_to_code
from ..defines import CONF_MAIN, CONF_SCROLLBAR, literal
from ..lv_validation import lv_bool, lv_int, lv_text
from ..lvcode import LocalVariable, LvConditional, lv, lv_expr, lv_obj
from ..types import LvType, ObjUpdateAction, lv_obj_t
from . import Widget, WidgetType, get_widgets

CONF_LIST = "list"
CONF_CHECKABLE = "checkable"

lv_list_t = LvType("lv_list_t")


class ListType(WidgetType):
    """
    A plain wrapper around LVGL's native `lv_list`: a scrollable container meant to be
    populated at runtime, via the `lvgl.list.add_text`/`add_button`/`remove` actions,
    rather than a static `options:`-style list -- for content that isn't known until
    the device is running (sensor readings, WiFi scan results, and so on).
    """

    def __init__(self):
        super().__init__(CONF_LIST, lv_list_t, (CONF_MAIN, CONF_SCROLLBAR), {})


list_spec = ListType()

LIST_ID_SCHEMA = cv.Schema({cv.Required(CONF_ID): cv.use_id(lv_list_t)})


@automation.register_action(
    "lvgl.list.add_text",
    ObjUpdateAction,
    LIST_ID_SCHEMA.extend({cv.Required(CONF_TEXT): lv_text}),
    synchronous=True,
)
async def list_add_text_to_code(config, action_id, template_arg, args):
    widgets = await get_widgets(config)

    async def do_add_text(w: Widget):
        text = await lv_text.process(config[CONF_TEXT])
        lv.list_add_text(w.obj, text)

    return await action_to_code(
        widgets, do_add_text, action_id, template_arg, args, config
    )


@automation.register_action(
    "lvgl.list.add_button",
    ObjUpdateAction,
    LIST_ID_SCHEMA.extend(
        {
            cv.Required(CONF_TEXT): lv_text,
            cv.Optional(CONF_CHECKABLE, default=False): lv_bool,
        }
    ),
    synchronous=True,
)
async def list_add_button_to_code(config, action_id, template_arg, args):
    widgets = await get_widgets(config)

    async def do_add_button(w: Widget):
        text = await lv_text.process(config[CONF_TEXT])
        checkable = await lv_bool.process(config[CONF_CHECKABLE])
        with (
            LocalVariable(
                "list_btn", lv_obj_t, lv_expr.list_add_button(w.obj, nullptr, text)
            ) as btn,
            LvConditional(checkable),
        ):
            lv_obj.add_flag(btn, literal("LV_OBJ_FLAG_CHECKABLE"))

    return await action_to_code(
        widgets, do_add_button, action_id, template_arg, args, config
    )


@automation.register_action(
    "lvgl.list.remove",
    ObjUpdateAction,
    LIST_ID_SCHEMA.extend({cv.Optional(CONF_INDEX): cv.templatable(cv.int_)}),
    synchronous=True,
)
async def list_remove_to_code(config, action_id, template_arg, args):
    widgets = await get_widgets(config)

    async def do_remove(w: Widget):
        if CONF_INDEX in config:
            index = await lv_int.process(config[CONF_INDEX])
            with (
                LocalVariable(
                    "list_child", lv_obj_t, lv_expr.obj_get_child(w.obj, index)
                ) as child,
                LvConditional(child),
            ):
                lv.obj_del(child)
        else:
            # No index given: start over with an empty list.
            lv.obj_clean(w.obj)

    return await action_to_code(
        widgets, do_remove, action_id, template_arg, args, config
    )
