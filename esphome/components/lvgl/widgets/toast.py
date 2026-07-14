from esphome import automation, codegen as cg, config_validation as cv
from esphome.const import CONF_ID, CONF_MESSAGE, CONF_TYPE
from esphome.core import CORE, ID
from esphome.cpp_generator import TemplateArguments
from esphome.cpp_types import App

from ..defines import (
    CONF_FLEX_FLOW,
    CONF_IMAGE,
    CONF_LAYOUT,
    CONF_LVGL_ID,
    ToastInfra,
    add_define,
    add_lv_use,
    get_toast_infra,
    get_toast_requested,
    literal,
    request_toast,
    set_toast_infra,
)
from ..lv_validation import lv_color, lv_image, lv_text, opacity, pixels_or_percent
from ..lvcode import (
    LVGL_COMP_ARG,
    LambdaContext,
    LvglComponent,
    lv,
    lv_add,
    lv_assign,
    lv_expr,
    lv_obj,
    lv_Pvariable,
)
from ..types import LvAnimation, LvglAction, lv_coord_t, lv_obj_t
from . import Widget, WidgetType, add_widgets, set_obj_properties
from .img import img_spec
from .label import label_spec
from .obj import obj_spec

# The slide/fade transition itself is quick and fixed; only the hold time before
# auto-dismissing is worth a distinct constant, since it drives the "out" animation's
# start_delay.
TOAST_SLIDE_DURATION_MS = 250
TOAST_HOLD_DURATION_MS = 3000


def _mark_toast_used(value):
    request_toast()
    return value


TOAST_ACTION_SCHEMA = cv.All(
    cv.maybe_simple_value(
        {
            cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
            cv.Required(CONF_MESSAGE): lv_text,
            cv.Optional(CONF_IMAGE): lv_image,
        },
        key=CONF_MESSAGE,
    ),
    _mark_toast_used,
)


async def _register_internal_component(var) -> None:
    # cg.register_component() asserts the var's id went through the normal
    # YAML-declared-ID validation pass (CORE.component_ids, populated by scanning the
    # parsed config tree) - which these internally synthesized animation instances,
    # never declared in any YAML, don't appear in. Do what register_component() actually
    # does for us (register with App, count it for the loop()-count define) without that
    # assertion.
    cg.add(App.register_component_(var))
    CORE.data.setdefault("looping_component_entries", []).append(str(var.base.type))


async def _toast_anim_values(y: float, opa: float) -> list:
    y_value = await pixels_or_percent.process(y)
    opa_value = await opacity.process(opa)
    return [literal(f"TemplatableValue<lv_coord_t>({v})") for v in (y_value, opa_value)]


async def _build_toast_animation(
    anim_id: ID,
    container_var,
    y_from: float,
    y_to: float,
    opa_from: float,
    opa_to: float,
    start_delay_ms: int,
) -> None:
    async with LambdaContext(
        [(lv_coord_t.operator("const").operator("ptr"), "values")]
    ) as ctx:
        lv_obj.set_style_translate_y(container_var, literal("values[0]"), 0)
        lv_obj.set_style_opa(
            container_var, literal("static_cast<lv_opa_t>(values[1])"), 0
        )
    froms = await _toast_anim_values(y_from, opa_from)
    tos = await _toast_anim_values(y_to, opa_to)
    var = cg.new_Pvariable(
        anim_id, TemplateArguments(2, False), await ctx.get_lambda(), froms, tos
    )
    cg.add(var.set_duration(TOAST_SLIDE_DURATION_MS))
    if start_delay_ms:
        cg.add(var.set_start_delay(start_delay_ms))
    await _register_internal_component(var)


async def _build_static_widget(
    widget_type: WidgetType, wid: ID, parent, config: dict
) -> Widget:
    # Mirrors WidgetType.create_to_code, minus add_line_marks() - these widgets are
    # synthesized internally rather than declared in the user's YAML, so there is no
    # source line for it to look up. All three widget types used here (obj/img/label)
    # are plain (non-compound), so only that branch of create_to_code is needed.
    add_lv_use(widget_type.name)
    add_lv_use(*widget_type.get_uses())
    creator = await widget_type.obj_creator(parent, config)
    var = lv_Pvariable(lv_obj_t, wid)
    lv_assign(var, creator)
    await widget_type.on_create(var, config)
    w = Widget.create(wid, var, widget_type, config)
    await set_obj_properties(w, config)
    await add_widgets(w, config)
    await widget_type.to_code(w, config)
    return w


async def toast_infra_to_code(lv_component, lvgl_id: ID) -> None:
    """
    Build the (singleton, hidden) toast container - a flex row holding an optional icon
    and a label, anchored to the bottom of the screen - plus the pair of LvAnimation
    instances that slide/fade it in and back out. Built once per ``lvgl:`` instance, only
    if ``lvgl.toast`` is actually used somewhere in the config.
    """
    add_define("USE_LVGL_ANIMATION")
    suffix = lvgl_id.id
    top_layer = lv_expr.disp_get_layer_top(lv_component.get_disp())

    container_id = ID(
        f"lv_toast_container_{suffix}", is_declaration=True, type=lv_obj_t
    )
    container = await _build_static_widget(
        obj_spec,
        container_id,
        top_layer,
        {
            CONF_ID: container_id,
            CONF_LAYOUT: {CONF_TYPE: "flex", CONF_FLEX_FLOW: "LV_FLEX_FLOW_ROW"},
        },
    )
    lv_obj.set_style_align(container.obj, literal("LV_ALIGN_BOTTOM_MID"), 0)
    container.set_style("bg_color", lv_color.black, 0)
    container.set_style("bg_opa", await opacity.process(0.85), 0)
    container.set_style("border_width", 0, 0)
    container.set_style("radius", 8, 0)
    container.set_style("pad_all", 8, 0)
    container.set_style("pad_column", 6, 0)
    # Parked off-screen (one container-height below the bottom edge) and fully
    # transparent until the first toast is shown.
    container.set_style("translate_y", await pixels_or_percent.process(1.0), 0)
    container.set_style("opa", await opacity.process(0.0), 0)

    img_id = ID(f"lv_toast_icon_{suffix}", is_declaration=True, type=lv_obj_t)
    img_w = await _build_static_widget(
        img_spec, img_id, container.obj, {CONF_ID: img_id}
    )
    img_w.add_flag("LV_OBJ_FLAG_HIDDEN")

    label_id = ID(f"lv_toast_label_{suffix}", is_declaration=True, type=lv_obj_t)
    await _build_static_widget(label_spec, label_id, container.obj, {CONF_ID: label_id})

    in_anim_id = ID(f"lv_toast_anim_in_{suffix}", is_declaration=True, type=LvAnimation)
    await _build_toast_animation(in_anim_id, container.obj, 1.0, 0.0, 0.0, 1.0, 0)

    out_anim_id = ID(
        f"lv_toast_anim_out_{suffix}", is_declaration=True, type=LvAnimation
    )
    await _build_toast_animation(
        out_anim_id, container.obj, 0.0, 1.0, 1.0, 0.0, TOAST_HOLD_DURATION_MS
    )

    set_toast_infra(
        lvgl_id,
        ToastInfra(
            container_id=container_id,
            img_id=img_id,
            label_id=label_id,
            in_anim_id=in_anim_id,
            out_anim_id=out_anim_id,
        ),
    )


async def toasts_to_code(lv_component, lvgl_id: ID) -> None:
    if get_toast_requested():
        await toast_infra_to_code(lv_component, lvgl_id)


@automation.register_action(
    "lvgl.toast",
    LvglAction,
    TOAST_ACTION_SCHEMA,
    synchronous=True,
)
async def toast_action_to_code(config, action_id, template_arg, args):
    infra = get_toast_infra(config[CONF_LVGL_ID])
    message = await lv_text.process(config[CONF_MESSAGE])
    image = config.get(CONF_IMAGE)
    async with LambdaContext(LVGL_COMP_ARG, where=action_id) as context:
        label_var = await cg.get_variable(infra.label_id)
        lv.label_set_text(label_var, message)
        img_var = await cg.get_variable(infra.img_id)
        if image is not None:
            lv.image_set_src(img_var, await lv_image.process(image))
            lv_obj.remove_flag(img_var, literal("LV_OBJ_FLAG_HIDDEN"))
        else:
            lv_obj.add_flag(img_var, literal("LV_OBJ_FLAG_HIDDEN"))
        in_anim = await cg.get_variable(infra.in_anim_id)
        out_anim = await cg.get_variable(infra.out_anim_id)
        lv_add(in_anim.start())
        lv_add(out_anim.start())
    var = cg.new_Pvariable(action_id, template_arg, await context.get_lambda())
    await cg.register_parented(var, config[CONF_LVGL_ID])
    return var
