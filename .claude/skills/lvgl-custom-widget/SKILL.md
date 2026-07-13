---
name: lvgl-custom-widget
description: Add a new custom LVGL widget type to ESPHome (a new key usable under an lvgl `widgets:` list), especially one whose content is populated at runtime by another component rather than fully known at YAML time. Use when asked to create/add an LVGL widget, or when a non-lvgl component needs to expose a widget to the `lvgl:` config.
allowed-tools: Read, Edit, Write, Glob, Grep, Bash
---

# Adding a custom LVGL widget in ESPHome

This documents how `esphome/components/lvgl/` lets you register a brand new
widget type (a new key under `widgets:` in YAML).

## 0. Check first: does a generic, reusable primitive already cover it?

Before building a bespoke, purpose-specific widget (a stateful C++ class,
possibly its own component), check whether the need is really just "a
container whose content is only known at runtime" — in which case a
generic, reusable widget plus ordinary YAML automations/lambdas is usually
enough, and is a much smaller, more broadly useful addition than a new
purpose-built widget.

Concrete case study: a request for a "WiFi network chooser" widget (list
scan results, let the user check one or more) was first built as a bespoke
`wifi_chooser` widget/component with its own C++ class implementing
`WiFiScanResultsListener`, rebuilding the list on every scan. That got
rolled back in favor of a generic **`list`** widget (`esphome/components/
lvgl/widgets/lv_list.py`, §2 below) — a thin wrapper around LVGL's native
`lv_list` with `lvgl.list.add_text` / `add_button` / `remove` actions and no
C++ of its own at all. The WiFi-picker use case becomes a YAML example: a
`text_sensor`/`wifi_info` or a lambda-driven trigger populates the list by
calling those actions from `wifi::global_wifi_component->get_scan_result()`.
The generic widget is useful for *any* runtime-populated list (sensor
readings, a fetched JSON array, ...), not just WiFi, and needed zero new
C++, zero cross-component wiring, and none of the placement tradeoffs in §3.

The tell that you should reach for a generic primitive instead of a bespoke
widget: if making the bespoke widget "useful anyway" still requires the user
to write a sample YAML config with lambda code around it (to supply a
password, wire up a connect button, decide what "selected" means for their
use case, etc.), the bespoke widget wasn't saving them from writing that
YAML+lambda glue — it was just relocating a small, fixed part of it into
C++ for one specific use case. Move the reusable part (add/remove/populate)
into a generic widget, and leave the use-case-specific part (what to do with
a selection, where the data comes from) as the example YAML+lambda it was
always going to need.

Only fall back to a bespoke compound widget (§4-§6) when the widget
genuinely needs its own C++ *state* beyond "container of runtime-added
children" — e.g. it must implement a typed listener interface itself
(rather than being fed by a YAML-level trigger calling generic actions), or
it tracks state a lambda can't reasonably express.

## 1. The registration mechanism: `WidgetType`

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
simplest real example, `dropdown.py`/`roller.py` for ones with runtime state,
`lv_list.py` for a plain container populated via custom actions instead of a
static config-time list):

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
  wrapper around one native `lv_xxx_create` call.
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

**Gotcha: never name a widget's Python module file after a Python builtin**
(`list`, `dict`, `type`, `id`, `object`, ...). `esphome/components/lvgl/
__init__.py` auto-imports every file under `widgets/` via `pkgutil.
iter_modules`, and Python's import machinery always sets `parent_module.
<name> = submodule` as a side effect of importing `parent.name` — so a
submodule literally named `list` becomes an attribute `list` on the
`widgets` package module object, **shadowing the builtin `list` type inside
`widgets/__init__.py`'s own global namespace** for any code that runs after
it's imported. This breaks things far from the widget itself in a confusing
way — e.g. `isinstance(x, list)` inside `get_widgets()` (used by every other
widget's actions) starts raising `TypeError: isinstance() arg 2 must be a
type...` because `list` no longer means the builtin. The `list` widget's
file is `lv_list.py`; only the YAML key (`CONF_LIST = "list"`) is the bare
word.

## 2. Runtime-populated containers: the generic `list` widget

For "a container whose items aren't known until runtime," reach for
`esphome/components/lvgl/widgets/lv_list.py` before writing anything new. It
wraps LVGL's native `lv_list` (`obj_creator` overridden to call
`lv_list_create` — `LV_USE_LIST` is unconditionally on in ESPHome's
`lv_conf.h`) and exposes three actions instead of a static `options:` list:

- `lvgl.list.add_text: {id, text}` → `lv_list_add_text(obj, text)` — a
  non-interactive row (e.g. a section header).
- `lvgl.list.add_button: {id, text, checkable: false}` → `lv_list_add_button
  (obj, nullptr, text)`, optionally flagged `LV_OBJ_FLAG_CHECKABLE`.
- `lvgl.list.remove: {id, index}` → deletes the child at `index`
  (`lv_obj_get_child` + `lv_obj_del`, guarded by a null check since LVGL
  returns null out of range); **omit `index` to `lv_obj_clean()` the whole
  list** — the common "start over" case for a rebuilt-on-refresh list.

All three are registered against the shared `ObjUpdateAction` C++ class (the
same generic action-running machinery every `lvgl.<widget>.update` action
already uses — see `esphome/components/lvgl/automation.py`'s `action_to_code`
helper), so no new C++ is needed at all for the widget itself.

This deliberately stops at "add/remove" — it does not try to solve
selection/click handling generically, since that varies per use case
(single vs. multi select, what "selected" should mean) and is exactly the
kind of thing a `!lambda` + `lv_obj_add_event_cb`/`lv_obj_has_state` in the
consuming YAML is the right tool for, not the widget.

## 3. If you really do need a bespoke widget: three placement options

Registering a widget just means instantiating a `WidgetType` subclass at
module level — that's a plain side effect of importing the module, and
nothing checks where the module physically lives. The only real constraint
is *when*: `any_widget_schema()` in `schemas.py` builds its schema **lazily,
at config-validation time, not at import time**, specifically so "external
components can register widgets before schema validation begins." So a
widget's Python file can live anywhere, as long as *something* imports it
before the `lvgl:` config block is validated. There are three genuinely
different places to put it, with different tradeoffs (worked out, then
rolled back, while building the `wifi_chooser` case study in §0):

**(a) `esphome/components/lvgl/widgets/`** — the path of least resistance.
`esphome/components/lvgl/__init__.py` does:

```python
for module_info in pkgutil.iter_modules(widgets.__path__):
    importlib.import_module(f".widgets.{module_info.name}", package=__package__)
```

Every `.py` file dropped into that directory is auto-imported whenever the
`lvgl` component is loaded — no registry edit needed, zero risk. This is
where essentially every stock widget's registration file lives, including
the generic `list` widget from §2. Fine when the widget is basically an lvgl
concern. Less fitting when the widget's whole *reason to exist* is another
component's data — the file ends up living somewhere that doesn't reflect
who really owns it, even if the C++ implementation lives elsewhere.

**(b) Bolted onto an existing, always-loaded component (e.g. `wifi/`)** —
tempting, but don't: since the widget module has to actually get imported by
*something*, you either import it unconditionally from that component's
`__init__.py` (which then pulls in the entire `lvgl` package — and its own
image/display/esp32 imports — for every config that uses the host component,
even the overwhelming majority that never touch `lvgl:`), or you gate the
import on a check of `CORE.raw_config` (fragile: nothing else in this
codebase relies on `raw_config` timing this way, and it silently breaks any
tool that imports components without going through normal config validation
— e.g. `script/build_language_schema.py` never sets `raw_config` at all, so
a widget gated this way vanishes from the generated docs/IDE schema with no
error).

**(c) A small, dedicated component of its own** — the cleanest answer when
the widget is genuinely owned by, or bridges, another domain and needs real
C++ state (i.e. §0's generic-primitive escape hatch doesn't apply). Give it
`DEPENDENCIES = ["wifi", "lvgl"]` (or whatever domains it bridges) and an
empty `CONFIG_SCHEMA = cv.Schema({})` — the user writes e.g. `my_widget:`
with no options, purely to opt in. That top-level key is what makes ESPHome
actually load the component (`CORE.loaded_integrations`, populated only by
the top-level/platform YAML-key loader — nothing else marks a component
"loaded" for the purpose of copying its `.h`/`.cpp` into the build, so a
component only ever reachable via a nested schema key, with no top-level key
of its own, would never get its C++ compiled in at all) and to import its
`__init__.py`, which registers the widget as a plain side effect. This is
provably safe regardless of YAML order: in `esphome/config.py`,
`MetadataValidationStep` (the step that actually validates each domain's
`CONFIG_SCHEMA`, including lvgl's own `widgets:` schema) has
`priority = -2.0`, explicitly lower than the default `0.0` used by
`LoadValidationStep` (the step that imports each domain's module), with the
comment "All components need to be loaded first to ensure dependency check
works" — so *every* domain's module import always completes before *any*
domain's schema validation begins, full stop. `DEPENDENCIES` also gives you
a clean, standard error ("Component my_widget requires component lvgl") if
the user forgets `lvgl:`, instead of the more obscure "Unknown widget type"
you'd get from option (a) with a manual `required_component` check. The cost
is a UX one: the user has to write an extra, option-free top-level key that
doesn't do anything by itself — a real but normal ESPHome pattern (see
`esphome/components/async_tcp/` for another schema-less marker component),
not a hidden gotcha.

Whichever placement, the file can be a *thin shim*: import the real C++
class and Python helpers from the owning component (or, for (c), keep the
class and the registration in the same new component).

**Watch out for `script/build_codeowners.py` (and similar directory-scanning
scripts) if you go with (c) and your component's C++ source file happens to
share a stem with the component's own directory name** (e.g.
`my_widget/my_widget.h`) **or if your top-level code has any side-effecting,
non-idempotent registration.** That script iterates every file in a
component's directory (including `__init__.py` itself) and calls
`get_platform(stem, name)` on each as a naive way to discover sub-platforms.
`import pkg.__init__` is valid Python but distinct from `import pkg` — it
re-executes the package's `__init__.py` as a second, separate module. That's
silently harmless for components whose top-level code just defines
classes/schemas (redefining them twice is a no-op), but fatal for one that
does something like populate a global registry with a duplicate check
(exactly what `WidgetType.__init__` does). If you hit `EsphomeError:
Duplicate definition of widget type '...'` only from
`script/build_codeowners.py` (not from `esphome config`/`compile`), this is
almost certainly why — the fix is in that script (skip
`platform_path.name == "__init__.py"` before calling `get_platform`), not in
your widget.

## 4. Compound widgets (state beyond a stock `lv_obj_t`)

Most existing widgets are thin: `w_type` is `LvType("lv_something_t")` and
`Widget.obj is Widget.var` (the C var IS the `lv_obj_t*`). But when a widget
needs its own C++ state (callbacks, a listener interface, anything beyond
"container of runtime-added children" — see §0 first), make it a
**compound widget**:

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

For per-item state (e.g. mapping a clicked button back to the item it
represents), store an index via `lv_obj_set_user_data(btn,
reinterpret_cast<void *>(index))` and keep the backing data in a
`FixedVector<T>` sized with `.init(n)` at rebuild time (per this repo's
heap-allocation guidance: runtime-known size → `FixedVector`, not
`std::vector`/`std::map`) — never a `std::string`-per-node allocated
scheme when a plain index will do. LVGL toggles `LV_STATE_CHECKED`
automatically for a button with `LV_OBJ_FLAG_CHECKABLE` before your
`LV_EVENT_CLICKED` handler runs, so `lv_obj_has_state(btn, LV_STATE_CHECKED)`
in the handler already reflects the new state; for single-select behavior,
uncheck sibling buttons yourself in that same handler.

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

If your use case is "populate a list from this data," though, check §0/§2
first: a YAML trigger (e.g. a `text_sensor`'s `on_value`, or any other
automation) calling the generic `list` widget's `add_text`/`add_button`/
`remove` actions from a `!lambda` often removes the need for a compound
widget and a listener interface entirely.

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
- For a widget living in `lvgl/widgets/` (§0/§1(a)), exercise it as an entry
  in the shared `tests/components/lvgl/lvgl-package.yaml` widget list
  alongside every other widget (e.g. the `list` widget's `add_text`/
  `add_button`/`remove` actions are wired into an `on_click` there) rather
  than creating a new per-widget test directory.
- For a dedicated component (§1(c)), it gets its own
  `tests/components/<component>/` directory per the normal component-test
  layout in root `CLAUDE.md` §6.
- Add one `validate.*.yaml` for schema-only edge cases if relevant.
