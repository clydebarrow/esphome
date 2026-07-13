from esphome import automation
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_INDEX, CONF_ON_UPDATE, CONF_ON_VALUE, CONF_TEXT
from esphome.cpp_types import nullptr

from ..automation import action_to_code
from ..defines import (
    CONF_MAIN,
    CONF_SCROLLBAR,
    CONF_WIDGETS,
    LV_EVENT_TRIGGERS,
    add_lv_use,
    literal,
)
from ..lv_validation import lv_bool, lv_int, lv_text
from ..lvcode import UPDATE_EVENT, LocalVariable, LvConditional, lv, lv_expr, lv_obj
from ..schemas import WIDGET_TYPES, any_widget_schema
from ..trigger import add_trigger
from ..types import LV_EVENT, LvType, ObjUpdateAction, lv_obj_t
from . import Widget, WidgetType, get_widgets, set_obj_properties

CONF_LIST = "list"
CONF_CHECKABLE = "checkable"
CONF_WIDGET = "widget"

lv_list_t = LvType("lv_list_t")


class ListType(WidgetType):
    """
    A plain wrapper around LVGL's native `lv_list`: a scrollable container meant to be
    populated at runtime, via the `lvgl.list.add_text`/`add_button`/`add`/`remove`/`clear`
    actions, rather than a static `options:`-style list -- for content that isn't known
    until the device is running (sensor readings, WiFi scan results, and so on).
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


def _reject_compound_widgets(value):
    """
    lvgl.list.add builds this hierarchy fresh, from local variables, every time the
    action runs, rather than once at boot from static globals. Compound widgets
    (dropdown, roller, keyboard, buttonmatrix, ...) carry their own C++ state
    constructed via placement-new into static storage, which is only safe to do
    once - so they can't be created repeatedly here.
    """
    for entry in value:
        w_type_name, w_conf = next(iter(entry.items()))
        if WIDGET_TYPES[w_type_name].is_compound():
            raise cv.Invalid(
                f"'{w_type_name}' can't be used inside lvgl.list.add: it keeps its "
                "own C++ state and isn't safe to create repeatedly at runtime."
            )
        _reject_compound_widgets(w_conf.get(CONF_WIDGETS, ()))
    return value


LIST_ADD_SCHEMA = LIST_ID_SCHEMA.extend(
    {
        cv.Required(CONF_WIDGET): cv.All(
            any_widget_schema(),
            cv.Length(min=1, max=1),
            _reject_compound_widgets,
        ),
    }
)


@automation.register_action(
    "lvgl.list.add",
    ObjUpdateAction,
    LIST_ADD_SCHEMA,
    synchronous=True,
)
async def list_add_to_code(config, action_id, template_arg, args):
    widgets = await get_widgets(config)

    async def do_add(w: Widget):
        [(w_type_name, w_conf)] = config[CONF_WIDGET][0].items()
        await _build_dynamic_widget(w_type_name, w_conf, w.obj)

    return await action_to_code(widgets, do_add, action_id, template_arg, args, config)


async def _build_dynamic_widget(w_type_name: str, w_conf: dict, parent) -> None:
    """
    Create one widget - and, recursively, its children and triggers - from scratch,
    inside whatever lambda context is currently active (the enclosing action), using
    a local variable rather than the usual global Pvariable. That's what lets this run
    again, fresh, each time the action executes instead of once at boot; property
    values are still free to be lambdas referencing the enclosing action's own
    parameters, since value processing already inherits the active lambda context
    (see LValidator.process()/get_lambda_context_args()). Triggers declared here
    (on_click, on_value, ...) fire with their own widget-level arguments only, the
    same as any statically-declared widget - not the enclosing action's parameters,
    since LVGL's C event-callback API has no way to carry extra closure state.
    """
    widget_type = WIDGET_TYPES[w_type_name]
    add_lv_use(w_type_name)
    add_lv_use(*widget_type.get_uses())
    creator = await widget_type.obj_creator(parent, w_conf)
    with LocalVariable(f"dyn_{w_type_name}", lv_obj_t, creator) as var:
        w = Widget(var, widget_type, w_conf)
        await set_obj_properties(w, w_conf)
        await widget_type.to_code(w, w_conf)
        await _wire_dynamic_triggers(w, w_conf)
        for child in w_conf.get(CONF_WIDGETS, ()):
            [(child_type, child_conf)] = child.items()
            await _build_dynamic_widget(child_type, child_conf, var)


async def _wire_dynamic_triggers(w: Widget, config: dict) -> None:
    """Mirrors the per-widget body of trigger.py's generate_triggers(), but wires
    each trigger immediately (inline, in the current context) instead of deferring
    to that later, boot-time-only pass - which never sees widgets created here since
    they're never registered in the global widget map.

    add_trigger's own callback lambda is necessarily captureless (it's handed to
    LVGL as a raw C function pointer, which can't carry closure state), so unlike a
    normal, globally-declared widget it can't reference our LocalVariable-based
    w.obj directly from *inside* the callback body - that variable is out of scope
    there (though it's still valid, and still used, for *registering* the callback
    on, via attach_obj, since that call happens outside the callback). Give
    add_trigger a proxy widget whose .obj instead recovers the object from the
    event itself (lv_event_get_target) for anything computed *inside* the callback
    (e.g. a checkable widget's checked state) - the event always fires on the same
    object either way, so this still resolves correctly.
    """
    event_target = Widget(
        literal("static_cast<lv_obj_t *>(lv_event_get_target(event))"), w.type, config
    )
    for event, conf in {
        event: conf for event, conf in config.items() if event in LV_EVENT_TRIGGERS
    }.items():
        w.add_flag("LV_OBJ_FLAG_CLICKABLE")
        await add_trigger(conf[0], event_target, event, attach_obj=w.obj)
    for conf in config.get(CONF_ON_VALUE, ()):
        await add_trigger(
            conf, event_target, LV_EVENT.VALUE_CHANGED, UPDATE_EVENT, attach_obj=w.obj
        )
    for conf in config.get(CONF_ON_UPDATE, ()):
        await add_trigger(conf, event_target, UPDATE_EVENT, attach_obj=w.obj)


@automation.register_action(
    "lvgl.list.remove",
    ObjUpdateAction,
    LIST_ID_SCHEMA.extend({cv.Required(CONF_INDEX): cv.templatable(cv.int_)}),
    synchronous=True,
)
async def list_remove_to_code(config, action_id, template_arg, args):
    widgets = await get_widgets(config)

    async def do_remove(w: Widget):
        index = await lv_int.process(config[CONF_INDEX])
        # lv_obj_del recursively destroys the whole subtree under the removed entry,
        # so a hierarchy added via lvgl.list.add is always cleaned up in full.
        with (
            LocalVariable(
                "list_child", lv_obj_t, lv_expr.obj_get_child(w.obj, index)
            ) as child,
            LvConditional(child),
        ):
            lv.obj_del(child)

    return await action_to_code(
        widgets, do_remove, action_id, template_arg, args, config
    )


@automation.register_action(
    "lvgl.list.clear",
    ObjUpdateAction,
    LIST_ID_SCHEMA,
    synchronous=True,
)
async def list_clear_to_code(config, action_id, template_arg, args):
    widgets = await get_widgets(config)

    async def do_clear(w: Widget):
        # lv_obj_clean recursively destroys every child's whole subtree, same as
        # lv_obj_del does for a single entry in lvgl.list.remove.
        lv.obj_clean(w.obj)

    return await action_to_code(
        widgets, do_clear, action_id, template_arg, args, config
    )
