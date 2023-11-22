import esphome.core as core
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_VALUE
from . import (
    PART_SCHEMA,
    lvgl_ns,
    LABEL_SCHEMA,
    lv_label_t,
    set_obj_properties,
    lv_obj_t,
    CONF_TEXT,
    get_text_lambda,
    lv_slider_t,
    BAR_SCHEMA,
    CONF_ANIMATED,
    get_value_lambda,
)

ObjModifyAction = lvgl_ns.class_("ObjModifyAction", automation.Action)


def modify_schema(lv_type, extras=None):
    schema = PART_SCHEMA.extend(
        {
            cv.Required(CONF_ID): cv.use_id(lv_type),
        }
    )
    if extras is None:
        return schema
    return schema.extend(extras)


@automation.register_action("lvgl.obj.update", ObjModifyAction, modify_schema(lv_obj_t))
async def obj_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = set_obj_properties(obj, config)
    lamb = await cg.process_lambda(core.Lambda(";\n".join([*init, ""])), [])
    var = cg.new_Pvariable(action_id, template_arg, lamb)
    return var


@automation.register_action(
    "lvgl.label.update", ObjModifyAction, modify_schema(lv_label_t, LABEL_SCHEMA)
)
async def label_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = set_obj_properties(obj, config)
    if CONF_TEXT in config:
        (value, lamb) = await get_text_lambda(config[CONF_TEXT])
        if value is not None:
            init.append(f'lv_label_set_text({obj}, "{value}")')
        if lamb is not None:
            init.append(f"lv_label_set_text({obj}, {lamb}())")
    lamb = await cg.process_lambda(core.Lambda(";\n".join([*init, ""])), [])
    var = cg.new_Pvariable(action_id, template_arg, lamb)
    return var


@automation.register_action(
    "lvgl.slider.update", ObjModifyAction, modify_schema(lv_slider_t, BAR_SCHEMA)
)
async def slider_update_to_code(config, action_id, template_arg, args):
    obj = await cg.get_variable(config[CONF_ID])
    init = set_obj_properties(obj, config)
    animated = config[CONF_ANIMATED]
    if CONF_VALUE in config:
        (value, lamb) = await get_value_lambda(config[CONF_VALUE])
        if value is not None:
            init.append(f'lv_slider_set_value({obj}, "{value}, {animated}")')
        if lamb is not None:
            init.append(f"lv_slider_set_value({obj}, {lamb}(), {animated})")
    lamb = await cg.process_lambda(core.Lambda(";\n".join([*init, ""])), [])
    var = cg.new_Pvariable(action_id, template_arg, lamb)
    return var
