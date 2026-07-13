---
name: lvgl-custom-widget
description: Add a new custom LVGL widget type to ESPHome (a new key usable under an lvgl `widgets:` list), especially one whose content is populated at runtime by another component rather than fully known at YAML time. Use when asked to create/add an LVGL widget, or when a non-lvgl component needs to expose a widget to the `lvgl:` config.
allowed-tools: Read, Edit, Write, Glob, Grep, Bash
---

# Adding a custom LVGL widget in ESPHome

This documents how `esphome/components/lvgl/` lets you register a brand new
widget type (a new key under `widgets:` in YAML), including the case where the
widget's real behavior belongs to a different component (e.g. a `wifi_chooser`
widget that is really "owned" by the `wifi` component).

## 1. Where the Python file lives: only "imported before validation" matters

Registering a widget just means instantiating a `WidgetType` subclass at
module level (see below) — that's a plain side effect of importing the
module, and nothing checks where the module physically lives. The only real
constraint is *when*: `any_widget_schema()` in `schemas.py` builds its schema
**lazily, at config-validation time, not at import time**, specifically so
"external components can register widgets before schema validation begins."
So a widget's Python file can live anywhere, as long as *something* imports
it before the `lvgl:` config block is validated.

`esphome/components/lvgl/widgets/` gets one such import path for free:

```python
# esphome/components/lvgl/__init__.py
for module_info in pkgutil.iter_modules(widgets.__path__):
    importlib.import_module(f".widgets.{module_info.name}", package=__package__)
```

Every `.py` file dropped into that directory is auto-imported whenever the
`lvgl` component is loaded — no central registry edit needed. This is why
essentially every existing widget's registration file lives there: it's the
path of least resistance, not a hard requirement. A widget could equally
live under the owning component's own directory (e.g.
`esphome/components/wifi/lvgl_wifi_chooser.py`) as long as that component's
`__init__.py` imports it (directly, or conditionally e.g. only when `lvgl`
is also configured) before `lvgl:` validation runs — there is just no
existing precedent for that path in this codebase, so it's less
battle-tested and you're responsible for getting the import ordering right
yourself, rather than relying on the auto-import.

Either way, the file can be a *thin shim*: import the real C++ class and
Python helpers from the owning component (e.g. `from esphome.components
import wifi`) the same way `lvgl/widgets/meter.py` requires `"image"` for
image indicators, or `lvgl/select/lvgl_select.h` bridges to
`esphome.components.select`. Put the actual behavior (the C++ class, its
`.h`, business logic) in the owning component's own directory (e.g.
`esphome/components/wifi/lvgl_wifi_chooser.h`), and keep the lvgl-side
schema/codegen glue as small as possible regardless of which directory it
sits in.

## 2. The registration mechanism: `WidgetType`

`esphome/components/lvgl/widgets/__init__.py` defines two central classes:

- **`WidgetType`**: describes a widget *type* (its name, C++ type, schema,
  code generators). Instantiating one (unless `is_mock=True`) inserts it into
  the global `WIDGET_TYPES` dict, which is what makes `widgets: - my_widget:`
  a valid YAML key, raising `EsphomeError` on a duplicate name. It also
  auto-registers an `lvgl.<name>.update` action for you.
- **`Widget`**: wraps one *instance* of a widget once created (its C++
  variable, its type, its config), with helpers like `set_property`,
  `set_style`, `add_flag`, `get_value`.

Minimal skeleton (see `esphome/components/lvgl/widgets/checkbox.py` for the
simplest real example, `dropdown.py`/`roller.py` for ones with runtime state):

```python
from ..types import LvType, LvCompound
from . import Widget, WidgetType

CONF_MY_WIDGET = "my_widget"
lv_my_widget_t = LvType("lv_my_widget_t")  # or a custom class name, see below

class MyWidgetType(WidgetType):
    def __init__(self):
        super().__init__(CONF_MY_WIDGET, lv_my_widget_t, (CONF_MAIN,), MY_SCHEMA)

    async def to_code(self, w: Widget, config):
        # Called both at creation and (with modify_schema) for update actions.
        ...

my_widget_spec = MyWidgetType()
```

Key `WidgetType` hooks worth knowing:
- `obj_creator(self, parent, config)` — returns the expression that
  constructs the underlying `lv_obj_t*`. Default is
  `lv_<lv_name>_create(parent)`. Override this if your widget isn't a plain
  wrapper around one native `lv_xxx_create` call (e.g. it should create a
  stock `lv_list` but under a different registered name — see §4).
- `on_create(self, var, config)` — `var` is the **raw `lv_obj_t*`** (even for
  compound widgets, this receives `widget->obj`, not the compound pointer).
- `to_code(self, w: Widget, config)` — `w.var` is the actual codegen
  variable: for compound widgets that's your C++ class pointer, so this is
  where you call your own setter methods (`w.var.set_something(...)`).
- `get_uses(self)` — other widget names your widget's generated code
  references (adds `add_lv_use(...)` for each, which becomes a
  `USE_LVGL_<NAME>` define and an `LV_USE_<NAME>` entry in the generated
  `lv_conf.h`; harmless even for names with no matching native LVGL feature).
- `required_component` property — return a component name (e.g. `"wifi"`) to
  auto-wrap the widget's schema in `cv.requires_component(...)`.

## 3. Compound widgets (state beyond a stock `lv_obj_t`)

Most existing widgets are thin: `w_type` is `LvType("lv_something_t")` and
`Widget.obj is Widget.var` (the C var IS the `lv_obj_t*`). But when a widget
needs its own C++ state (dynamic content, callbacks, a listener interface),
make it a **compound widget**:

```cpp
// Parent class for things that wrap an LVGL object, in lvgl_esphome.h:
class LvCompound {
 public:
  virtual ~LvCompound() = default;
  virtual void set_obj(lv_obj_t *lv_obj) { this->obj = lv_obj; }
  lv_obj_t *obj{};
};
```

Declare your C++ class as `public LvCompound` (see `IndicatorLine`,
`LvLineType`, `LvSelectable` in `lvgl_esphome.h` for real examples), then in
Python:

```python
my_type_t = LvType("MyCppClass", parents=(LvCompound,))
```

`WidgetType.is_compound()` checks `w_type.inherits_from(LvCompound)`. When
true, `create_to_code` does:

```python
var = cg.new_Pvariable(wid)        # `MyCppClass *wid = new MyCppClass();`
lv_add(var.set_obj(creator))       # `wid->set_obj(lv_xxx_create(parent));`
await self.on_create(var.obj, config)   # var.obj == `wid->obj`
```

So override `set_obj()` in C++ to run your one-time setup (e.g. registering
a listener, building initial content) right when the widget receives its
underlying `lv_obj_t*`.

## 4. Widgets whose content is only known at runtime

Widgets like `dropdown`/`roller` take a static `options:` list at YAML time
and call `set_options(...)` once in `to_code`. If instead your widget's items
are discovered live on the device (sensor readings, scan results, a list
fetched over the network), don't try to force them through Python codegen —
skip per-item Python widget creation entirely:

1. Override `obj_creator()` to build a plain stock LVGL container that
   supports dynamic children, e.g. `lv_list_create(parent)`
   (`lv_list_add_button(list, icon, txt)` / `lv_list_add_text(list, txt)`
   add items and are always available — `LV_USE_LIST` is unconditionally on
   in ESPHome's `lv_conf.h`).
2. In your compound C++ class's `set_obj()`, register whatever runtime
   callback/listener produces the data (see §5).
3. In that callback, `lv_obj_clean(this->obj)` to clear old children, then
   rebuild with `lv_list_add_button`/`lv_list_add_text` from the live data.
4. For per-item state (e.g. mapping a clicked button back to the item it
   represents), store an index via `lv_obj_set_user_data(btn,
   reinterpret_cast<void *>(index))` and keep the backing data in a
   `FixedVector<T>` sized with `.init(n)` at rebuild time (per this repo's
   heap-allocation guidance: runtime-known size → `FixedVector`, not
   `std::vector`/`std::map`) — never a `std::string`-per-node allocated
   scheme when a plain index will do.
5. Selection state: LVGL toggles `LV_STATE_CHECKED` automatically for a
   button with `LV_OBJ_FLAG_CHECKABLE` before your `LV_EVENT_CLICKED`
   handler runs, so `lv_obj_has_state(btn, LV_STATE_CHECKED)` in the handler
   already reflects the new state. For single-select behavior, uncheck
   sibling buttons yourself in that same handler.

## 5. Consuming another component's runtime data from a widget

If the widget needs live data from a non-lvgl component (e.g. WiFi scan
results), prefer that component's **listener interface** over
`std::function` callbacks — this project treats `std::function` glue as
something to avoid where a typed listener interface already exists, and
several components already expose one:

```cpp
// esphome/components/wifi/wifi_component.h
class WiFiScanResultsListener {
 public:
  virtual void on_wifi_scan_results(const wifi_scan_vector_t<WiFiScanResult> &results) = 0;
};
```

The doc comment on the interface is explicit about the Python-side contract:
"Components must call wifi.request_wifi_scan_results_listener() in their
Python to_code()." That call increments a counter in `CORE.data`; a later
`final_step()` in `wifi/__init__.py` turns the total count into
`USE_WIFI_SCAN_RESULTS_LISTENERS` + `ESPHOME_WIFI_SCAN_RESULTS_LISTENERS`
defines, which is what makes `add_scan_results_listener()` and the
`StaticVector` that backs it compile in at all. Skipping the Python call
means the interface method exists but nothing ever calls it.

Concrete existing precedent to copy: `esphome/components/wifi_info/` (its
`text_sensor.py` calls `wifi.request_wifi_scan_results_listener()`, and
`ScanResultsWiFiInfo` in `wifi_info_text_sensor.h/.cpp` implements
`WiFiScanResultsListener` and calls
`wifi::global_wifi_component->add_scan_results_listener(this)`). For a
compound LVGL widget, do the `add_scan_results_listener(this)` call inside
your `set_obj()` override instead of a `Component::setup()` — the widget
usually isn't registered as its own `Component`, and `global_wifi_component`
is already valid by the time any widget's generated code runs inside
`App.setup()`.

## 6. Automations: prefer `build_callback_automation`

If your widget needs to fire a YAML `on_xxx:` automation and doesn't need
per-trigger mutable state (edge detection, timers), skip the `Trigger<Ts...>`
subclass entirely and use the callback-forwarder path (see root `CLAUDE.md`
§4 for the full pattern):

```python
from esphome import automation
...
for conf in config.get(CONF_ON_SELECT, []):
    await automation.build_callback_automation(
        w.var, "add_on_select_callback",
        [(cg.std_string, "ssid"), (cg.bool_, "selected")], conf,
    )
```

which requires only that your C++ class expose a templated registration
method:

```cpp
template<typename F> void add_on_select_callback(F &&callback) {
  this->select_callback_.add(std::forward<F>(callback));
}
LazyCallbackManager<void(std::string, bool)> select_callback_;
```

## 7. Testing

- `esphome config tests/components/<component>/test.<target>.yaml` to check
  schema validation.
- `script/test_build_components -c <component> -t <target>` to compile.
- Put the widget under `widgets:` in a page, exercising every config option
  at least once, plus one `validate.*.yaml` for schema-only edge cases if
  relevant (see root `CLAUDE.md` §6 on `validate.*.yaml` files).
