import sys

import esphome.codegen as cg
from . import (
    BTNMATRIX_CTRLS,
    CONF_ARC,
    CONF_SPINBOX,
)
from ...core import TimePeriod

EVENT_LAMB = "event_lamb__"


class Widget:
    def __init__(self, var, wtype: cg.MockObjClass, config: dict = None, obj=None):
        self.var = var
        self.type = wtype
        self.config = config
        self.obj = obj or var
        self.parent = None
        self.scale = 1.0
        self.step = 1.0
        self.range_from = -sys.maxsize
        self.range_to = sys.maxsize

    def set_parent(self, parent):
        self.parent = parent

    def add_state(self, state):
        return [f"lv_obj_add_state({self.obj}, {state})"]

    def clear_state(self, state):
        return [f"lv_obj_clear_state({self.obj}, {state})"]

    def set_state(self, state, cond):
        return [
            f" if({cond}) lv_obj_add_state({self.obj}, {state}); else lv_obj_clear_state({self.obj}, {state})"
        ]

    def has_state(self, state):
        """
        Return code that checks for a givens state
        :param state: A state bit
        :return:
        """
        return f"(lv_obj_get_state({self.obj}) & ({state})) != 0"

    def is_pressed(self):
        return self.has_state("LV_STATE_PRESSED")

    def add_flag(self, flag):
        return [f"lv_obj_add_flag({self.obj}, {flag})"]

    def clear_flag(self, flag):
        return [f"lv_obj_clear_flag({self.obj}, {flag})"]

    def set_property(self, prop, value, ltype=None):
        if isinstance(value, dict):
            value = value.get(prop)
        if value is None:
            return []
        if isinstance(value, TimePeriod):
            value = value.total_milliseconds
        ltype = ltype or self.type_base()
        return [f"lv_{ltype}_set_{prop}({self.obj}, {value})"]

    def set_style(self, prop, value, state):
        return [f"lv_obj_set_style_{prop}({self.obj}, {value}, {state})"]

    def set_event_cb(self, code, *varargs):
        init = add_temp_var("event_callback_t", EVENT_LAMB)
        init.extend([f"{EVENT_LAMB} = [](lv_event_t *e) {{ {code} ;}} \n"])
        for arg in varargs:
            init.extend(
                [
                    f"lv_obj_add_event_cb({self.obj}, {EVENT_LAMB}, {arg}, nullptr)",
                ]
            )
        return init

    def get_value(self):
        if self.scale == 1.0:
            return f"lv_{self.type_base()}_get_value({self.obj})"
        return f"lv_{self.type_base()}_get_value({self.obj})/{self.scale:#f}f"

    def set_value(self, value, animated: bool = False):
        if self.type_base() in (CONF_ARC, CONF_SPINBOX):
            animated = ""
        else:
            animated = f", {animated}"
        if self.scale != 1.0:
            value = f"{value} * {self.scale:#f}"
        return [f"lv_{self.type_base()}_set_value({self.obj}, {value} {animated})"]

    def get_mxx_value(self, which: str):
        if self.scale == 1.0:
            mult = ""
        else:
            mult = f"/ {self.scale:#f}"
        if self.type_base() == CONF_SPINBOX and (which == "max" or which == "min"):
            gval = f"((lv_spinbox_t *){self.obj})->range_{which}"
        else:
            gval = f"lv_{self.type_base()}_get_{which}_value({self.obj})"
        return f"({gval} {mult})"

    def get_max_value(self):
        return self.get_mxx_value("max")

    def get_min_value(self):
        return self.get_mxx_value("min")

    def type_base(self):
        base = str(self.type)
        if base.startswith("Lv"):
            return f"{self.type}".removeprefix("Lv").removesuffix("Type").lower()
        return f"{self.type}".removeprefix("lv_").removesuffix("_t")

    def __str__(self):
        return f"({self.var}, {self.type})"


class MatrixButton(Widget):
    """
    Describes a button within a button matrix.
    """

    def __init__(self, btnm, btype, config, index):
        super().__init__(btnm, btype, config)
        self.index = index

    @staticmethod
    def map_ctrls(ctrls):
        clist = []
        for item in ctrls:
            item = item.upper().removeprefix("LV_BTNMATRIX_CTRL_")
            assert item in BTNMATRIX_CTRLS
            clist.append(f"(int)LV_BTNMATRIX_CTRL_{item}")
        return "|".join(clist)

    def set_ctrls(self, *ctrls):
        return [
            f"lv_btnmatrix_set_btn_ctrl({self.var.obj}, {self.index}, {self.map_ctrls(ctrls)})"
        ]

    def clear_ctrls(self, *ctrls):
        return [
            f"lv_btnmatrix_clear_btn_ctrl({self.var.obj}, {self.index}, {self.map_ctrls(ctrls)})"
        ]

    def add_flag(self, flag):
        if flag.endswith("CLICKABLE"):
            return self.var.add_flag(flag)
        flag = flag.removeprefix("LV_OBJ_FLAG_").upper()
        return self.set_ctrls(flag)

    def clear_flag(self, flag):
        return self.clear_ctrls(flag.removeprefix("LV_OBJ_FLAG_"))

    def add_state(self, state):
        return self.set_ctrls(state.removeprefix("LV_STATE_"))

    def clear_state(self, state):
        return self.clear_ctrls(state.removeprefix("LV_STATE_"))

    def set_state(self, state, cond):
        state = self.map_ctrls([state.removeprefix("LV_STATE_")])
        return [
            f"""if({cond}) lv_btnmatrix_set_btn_ctrl({self.var.obj}, {self.index}, {state}); else
        lv_btnmatrix_clear_btn_ctrl({self.var.obj}, {self.index}, {state})"""
        ]

    def index_check(self):
        return f"(lv_btnmatrix_get_selected_btn({self.var.obj}) == {self.index})"

    def has_state(self, state):
        state = self.map_ctrls([state.upper().removeprefix("LV_STATE_")])
        return f"(lv_btnmatrix_has_btn_ctrl({self.var.obj}, {self.index}, {state}))"

    def is_pressed(self):
        return f'({self.index_check()} && {self.var.has_state("LV_STATE_PRESSED")})'

    def set_event_cb(self, code, *varargs):
        init = add_temp_var("event_callback_t", EVENT_LAMB)
        init.append(
            f"""{EVENT_LAMB} = [](lv_event_t *e) {{
                    if {self.index_check()} {{
                        {code};
                    }}
                }}"""
        )
        for arg in varargs:
            init.append(
                f"lv_obj_add_event_cb({self.var.obj}, {EVENT_LAMB}, {arg}, nullptr)"
            )
        return init

    def set_selected(self):
        return [f"lv_btnmatrix_set_selected_btn({self.var.obj}, {self.index})"]

    def set_width(self, width):
        return [f"lv_btnmatrix_set_btn_width({self.var.obj}, {self.index}, {width})"]


lv_temp_vars = set()  # Temporary variables


def add_temp_var(var_type, var_name):
    if var_name in lv_temp_vars:
        return []
    lv_temp_vars.add(var_name)
    return [f"{var_type} * {var_name}"]
