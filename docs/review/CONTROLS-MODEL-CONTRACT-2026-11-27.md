# Controls Model Contract & Conversion Recipe (2026-11-27)

Foundation design, coexistence bridge, worked proof (`Knob` and `NumberField`), parallel per-family conversion inventory, and shared-header merge order for removing generic C++ control widgets from `shell-mpc/ui/` while keeping the parameter, Q-Link, and layout model intact.

Commits establishing this foundation on `agy/controls-model` (from `2d59b1d9`):
- `a828177c` — `feat(ui): add ControlModel contract in model.h and wire QGroup/Binder`
- `d9902358` — `refactor(ui): convert Knob and NumberField to ControlModel and delete their draw/touch code`
- `a9e63eaf` — `fix(ui): preserve Component widget pointer in Composite::addKid(ControlModel&)`
- `c101b0ad` — `test(ui): convert Knob and NumberField host tests to ControlModel contract`

---

## 1. Exact `ControlModel` Contract (`shell-mpc/ui/model.h`)

### 1.1 Design constraints & standalone check

`shell-mpc/ui/model.h` contains **zero drawing code, zero GPU/scene headers, zero theme tokens, and zero touch handling**. It includes only five C++17 standard library headers: `<algorithm>`, `<cmath>`, `<cstdint>`, `<cstdio>`, `<cstring>`.

Standalone header verification (requires **no** `-I` include paths):

```sh
c++ -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic -Werror -x c++-header shell-mpc/ui/model.h
```

### 1.2 Shared pure types moved from `shell-mpc/ui/core.h`

All in `namespace synthyboy_mpc::ui`:

| Symbol | Purpose |
|---|---|
| `Size { int w, h; }` | Minimum box dimension |
| `Rect { int x, y, w, h; }` | Cell geometry (`right()`, `bottom()`, `empty()`, `contains()`, `overlaps()`, `inset()`, `operator==`, `unite()`) |
| `ChangeKind`, `Change` | `None, Value, Index, Request, Depth, ModsRequest, AddRequest` (`turn()` and `wheel()` outputs) |
| `setText(char* dst, size_t cap, const char* src)` | UTF-8-safe bounded copy |
| `kNoSource`, `ModEntry`, `ModInput`, `TabState`, `tabState()`, `depthArc()`, `tabName()` | Modulation list, depth arc, and source tab state; `ModEntry::colour()` is declared in `model.h` and defined inline in `core.h` via `theme::sourceColour` so `model.h` stays theme-free |
| `ParamHandle { int id = 0; int param = -1; const char* key = ""; bound() }` | Shared page-id / engine-param-index / static-key handle |

### 1.3 `ControlModel` base class API (`shell-mpc/ui/model.h:160-231`)

Every generic control model (and, via the bridge, every legacy `Component` widget) satisfies `ControlModel`:

- **Lifetime / copy**: `ControlModel() = default; virtual ~ControlModel() = default;` (non-copyable; polymorphic so `dynamic_cast<Component*>` and `dynamic_cast<const Knob*>` work cleanly).
- **Identity & parameter binding handle**:
  - `id()`, `setId(int)`
  - `param()`, `setParam(int)`
  - `paramKey()`, `setParamKey(const char*)`
  - `paramHandle()`, `setParamHandle(const ParamHandle&)`
- **Native cell layout** (drives `nativeApertures()`, `isNativeControl()`, and host placement):
  - `Rect rect() const noexcept`
  - `virtual void place(const Rect& r)`
  - `virtual Size minSize() const`
  - `virtual bool sizeOk() const noexcept`
- **State flags**:
  - `enabled()`, `virtual void setEnabled(bool)`
  - `visible()`, `virtual void setVisible(bool)` (`void` return; kept virtual)
  - `inactive()`, `virtual void setInactive(bool)` (switched-off grey presentation metadata)
  - `qFrame()`, `qIndex()`, `virtual void setQ(bool frame, int index)` (Q-Link focus & 1..4 badge index)
- **Value / range / step / text formatting**:
  - `virtual double value() const noexcept` (default `0.0`)
  - `virtual double minValue() const noexcept` (default `0.0`)
  - `virtual double maxValue() const noexcept` (default `1.0`)
  - `virtual double stepValue() const noexcept` (default `0.0`)
  - `virtual double defaultValue() const noexcept` (default `0.0`)
  - `virtual void formatText(char* out, size_t cap) const` (`void` return; fills `out`)
- **Hardware Q-Link & wheel stepping**:
  - `virtual Change turn(double delta)` — checks `enabled_`, delegates to `protected: virtual Change onTurn(double)`
  - `Change wheel(int notches)` — returns `turn(static_cast<double>(notches) * wheelUnit())`
  - `virtual double wheelUnit() const noexcept` (default `1.0`; `1.0 / 128.0` for `KnobModel`)
  - `virtual bool wheelOver(int x, int y, int notches, Change& out)` — returns `false` if `!enabled_ || !rect_.contains(x, y)`
- **Tree & QGroup traversal**:
  - `virtual int collectModelLeaves(ControlModel** out, int n, int max)` — appends `this` by default; `Composite` overrides to recurse into all visible `kids_` (both pure models and legacy `Component` widgets)
  - `virtual ControlModel* primaryModel() const` — returns `nullptr` by default; `Composite` returns `primary_`

### 1.4 Model classes in `shell-mpc/ui/model.h`

- **`KnobModel : public ControlModel`** (`model.h:235-302`): normalised `0..1` rotary parameter model. Retains `State : ModInput`, `kLongPressMs = 600.0`, `minSize()` (`172x48` compact / `72x56` small / `172x72` side / `100x72` standard), `wheelUnit() = 1.0 / 128.0`, `hasStatus()`, `statusString()`, `modulated()`, `tab()`, `value/minValue/maxValue/stepValue/defaultValue/formatText`, and `onTurn()` with `state.led && !state.ledOn` gating and `[0, 1]` clamping. `base.h` aliases `using Knob = KnobModel;`.
- **`NumberFieldModel : public ControlModel`** (`model.h:306-376`): stepped discrete parameter model. Retains `Format`, `Icon`, `State : ModInput`, `maxBox`, `minSize() = {80, 56}`, static `formatValue()`, `numSteps()`, `indexOf()`, `value/minValue/maxValue/stepValue/defaultValue/formatText`, and `onTurn()` with `state.readOnly`, `state.notSet`, and `[min, max]` step clamping. `base.h` aliases `using NumberField = NumberFieldModel;`.

### 1.5 Coexistence bridge (mixed models + legacy widgets)

During incremental conversion across widget families, unconverted widgets (`Component` subclasses) and converted pure models (`ControlModel` subclasses) coexist seamlessly:

1. **`Component : public ControlModel`** (`shell-mpc/ui/core.h`): every legacy widget automatically satisfies `ControlModel*` parameters in `QGroup`, `Composite`, `mpckit::Binder`, and `mpckit::PageBase`.
2. **`QGroup`** (`shell-mpc/ui/qgroup.h`): stores `ControlModel* slot_[kSlots]`; exposes `set(int, ControlModel*)`, `member(int) -> ControlModel*`, and dual `check(...)` / `drawMappedMembers(...)` overloads for `ControlModel*` and `Component*`. `QGroup::turn()` routes directly to `ControlModel::turn()`.
3. **`Composite`** (`shell-mpc/ui/composite.h`): maintains parallel arrays `ControlModel* kids_[kMaxKids]` and `Component* widgetKids_[kMaxKids]` (and `primary_` / `primaryWidget_`).
   - `addKid(ControlModel& m, bool shown)` **must** cache `widgetKids_[n_] = dynamic_cast<Component*>(&m)` so containers such as `KnobPanel` (which calls `addKid(*m.comp)` through `unique_ptr<ControlModel>`) keep drawing and hit-testing any still-unconverted widget children (`Stepper`, `LedToggle`, `UnisonBox`, `WarpPair`, `MenuButton`).
   - `drawChildren()`, `drawChild()`, `touchDown()`, `touchTargets()`, and `collectLeaves()` skip entries where `widgetKids_[i] == nullptr` (pure models never draw or receive overlay touch).
   - `collectModelLeaves()` visits every visible entry in `kids_[]` (both pure models and widgets).
4. **`mpckit::Binder` & `mpckit::PageBase`** (`shell-mpc/kit/binder.h`, `shell-mpc/kit/page_base.h`): `Binder::Item::c` is `ui::ControlModel*`; `bind()` / `resolve()` populate `id`, `param`, and `paramKey` via `ControlModel` setters; `PageBase::bind()` overloads accept both `ui::Component&` (adds to `parts_`) and `ui::ControlModel&` (model-only binding); `PageBase::group()` takes `std::initializer_list<ui::ControlModel*>`.
5. **`ui::setHostPainted(ControlModel*, bool)`** (`shell-mpc/ui/core.h`): free helper that sets `hostPainted` when `dynamic_cast<Component*>(m)` succeeds and is a no-op for pure `ControlModel` instances, allowing overlay `*_ui.h` loops over mixed `Item::c` pointers to compile cleanly during migration.

---

## 2. Step-by-Step Conversion Recipe (Worked Example: `Knob` & `NumberField`)

Follow these 7 steps for each widget family:

1. **Add `<Widget>Model : public ControlModel` in `model.h` (or `model_<family>.h` while parallel workers run)**:
   - Move `State`, enums, `minSize()`, `wheelUnit()`, `value/minValue/maxValue/stepValue/defaultValue/formatText`, `onTurn()`, and pure state/formatting helpers.
   - Omit all drawing (`draw`, `drawBody`, `Geom`, `geom()`, angles/points, `theme::` calls) and touch/timer members (`touchDown`, `touchMove`, `touchUp`, `tick`, `tap_`, `drag_`, hit rects).
2. **Replace the widget class in its owning header (e.g., `shell-mpc/ui/base.h`) with a `using` alias and delete draw/touch**:
   ```cpp
   // shell-mpc/ui/base.h (commit d9902358: 695 lines deleted)
   -class Knob : public Component { ... };
   -class NumberField : public Component { ... };
   +using Knob = KnobModel;
   +using NumberField = NumberFieldModel;
   ```
   Simplify any trivial subclass in screen headers that only existed to stub out `draw`/`touch*`:
   ```cpp
   // shell-mpc/ui/filter_display.h (commit d9902358)
    class FilterKnob : public Knob
    {
    public:
        using Knob::Knob;
   -    void draw (gpu::SceneBuilder&) const override {}
   -    Change touchDown (int, int, double) override { return Change::none(); }
   -    Change touchMove (int, int) override { return Change::none(); }
   -    Change touchUp (int, int) override { return Change::none(); }
    };
   ```
3. **Update UI screen/panel headers (`shell-mpc/ui/*.h`)**:
   - Change local/member pointers and arrays holding `Component*` (such as `QGroup` member lists, `KnobPanel::part()`, `FxSlotEditor::memberOf()`, `BasicControls` pointers, `VoiceScreen` / `VoiceMaster` arrays, `ScalarCurveEditor` / `ShapeEditor` pointers) to `ControlModel*`.
   - Call `drawChild(sb, i)` or `if (auto* w = dynamic_cast<Component*>(c)) w->draw(sb)` instead of calling `->draw()` on `ControlModel*`.
4. **Update overlay page headers (`shell-mpc/overlay/*_ui.h`, `osc_spec_page.h`, etc.)**:
   - Update `paramOf(const ui::Component*)`, `groupMembers` locals, and arrays of mixed controls to `ui::ControlModel*`.
   - Route `setHostPainted` through `ui::setHostPainted(it.c, ...)` when iterating `Binder::Item::c`.
   - Watch `-Woverloaded-virtual`: if a page class defines `void setVisible(const bool*)` (as in `osc_spec_page.h`), add `using ui::Composite::setVisible;` because `ControlModel::setVisible(bool)` is virtual.
5. **Run header syntax checks before touching tests**:
   ```sh
   c++ -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic -Werror -x c++-header shell-mpc/ui/model.h
   c++ -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic -Werror \
     -I core/include -I core/filters-serum/include -I core/shapes/include \
     -I shell-mpc/overlay -I shell-mpc/ui \
     -x c++-header shell-mpc/ui/ui.h shell-mpc/kit/binder.h shell-mpc/kit/page_base.h shell-mpc/overlay/pages.h shell-mpc/overlay/scalar_curve_ui.h
   ```
6. **Convert covering host tests (`shell-mpc/ui/tests/*`) & fix affected overlay test callers (`shell-mpc/overlay/tests/*`)**:
   - Rewrite UI unit tests for the converted family against `ControlModel`: test `turn()`, `wheel()`, `wheelOver()`, `value/min/max/step/default/formatText`, `enabled/visible/inactive/qFrame/qIndex`, `place()/rect()/minSize()/sizeOk()`, and `paramHandle()` instead of pixel rendering or `touchDown/Move/Up/tick/Geom`.
   - Where tests inspect whole screens (`ui_composite_host.cpp`, `ui_screen_host.cpp`, `ui_lfo_host.cpp`), use `collectModelLeaves()` for count/bounds/non-overlap/`QGroup::check()`, and `dynamic_cast<Component*>` when checking overlay touch capture (`capturing()`).
   - Widen local test helpers (`tap`, `tapC`, `centre`, `inLeaves`, `isShown`, `sizesOk`, `covers`, `openMods`) from `const ui::Component&` / `ui::Component*` to `const ui::ControlModel&` / `ui::ControlModel*`.
   - Note: `formatText(buf, cap)` returns `void` (do not use it inside a boolean `check(k.formatText(...) && ...)` expression).
   - Run every covering UI host test **standalone** under ASan + UBSan and `-fsyntax-only` on affected overlay tests.
7. **Preserve required metadata (record, don't silently drop)**:
   - Keep state fields still read by `overlay/*_ui.h`, `kit/*`, or host publishers on `*Model::State` (see Section 5).

---

## 3. Per-Family Conversion List & Parallel Worker Map

Derived from `docs/review/OVERLAY-CONTROLS-REMOVAL-2026-11-27.md` and cross-checked with header includes in `shell-mpc/ui/` and `shell-mpc/overlay/`.

### 3.1 Widget families & non-control items

| # | Family (class / header) | Status / Verdict | UI Screen & Container Headers (`shell-mpc/ui/`) | Overlay Headers (`shell-mpc/overlay/`) | Covering Host Tests (`shell-mpc/ui/tests/`, `overlay/tests/`) |
|---|---|---|---|---|---|
| 1 | **`Knob`, `KnobModel`, `FilterKnob`, `NumberField`, `NumberFieldModel`** (`base.h`, `filter_display.h`) | **DONE** (`d9902358`, `a9e63eaf`, `c101b0ad`) | `base.h`, `composite.h`, `panel.h`, `screen.h`, `env_screen.h`, `filter_display.h`, `fx_slot_editor.h`, `lfo_parts.h`, `lfo_screen.h`, `mod_detail.h`, `scalar_curve_editor.h`, `shape_editor.h`, `voice_master.h`, `voice_screen.h` | `env_ui.h`, `filter_ui.h`, `lfo_ui.h`, `master_ui.h`, `osc_spec_page.h`, `osc_ui.h`, `voice_ui.h` | `ui_base_host`, `ui_cov_knobs_values_host`, `ui_cov_new_parts_host`, `ui_cov_qgroups_chips_host`, `ui_led_host`, `ui_composite_host`, `ui_display_host`, `ui_screen_host`, `ui_lfo_host`, `ui_fx_editor_host` |
| 2 | **`ToggleIcon`, `LedToggle`, `StripPower`, `LedSwitchProxy`** (`base.h`, `led_toggle.h`, `mixer_strip.h`, `lfo_screen.h`) | **CONVERT TO MODEL** (`DUP + Q model`; e.g. `smoothSwitch` is Q member `lfo_ui.h:1017`; Power/Bypass/Mute/Solo/Env1/Clean/Mono) | `base.h`, `led_toggle.h`, `mixer_strip.h`, `lfo_screen.h`, `lfo_parts.h`, `env_screen.h`, `filter_display.h`, `fx_slot_editor.h`, `mod_detail.h`, `mod_matrix.h`, `mod_targets.h`, `panel.h`, `scalar_curve_editor.h`, `screen.h`, `shape_editor.h`, `voice_master.h`, `voice_screen.h` | `env_ui.h`, `filter_ui.h`, `fx_ui.h`, `lfo_ui.h`, `master_ui.h`, `mixer_ui.h`, `osc_spec_page.h`, `osc_ui.h`, `voice_ui.h` | `ui_base_host`, `ui_led_host`, `ui_cov_toggles_buttons_host`, `ui_cov_new_parts_host`, `ui_composite_host`, `ui_screen_host`, `ui_lfo_host`, `ui_env_host`, `ui_fx_editor_host` |
| 3 | **`Stepper`, `LedList`** (`base.h`, `led_list.h`) | **CONVERT TO MODEL** (`DUP + Q model`; `qStepSelect`, mod filters, FX rack/steppers, Voice/Filter/LFO/OSC choice lists; keep `HOLD-UNCLEAR` note for `qStepSelect`/mod filters until host-verified) | `base.h`, `led_list.h`, `carousel.h`, `composite.h`, `env_screen.h`, `filter_display.h`, `fx_slot_editor.h`, `lfo_parts.h`, `lfo_screen.h`, `mod_matrix.h`, `mod_targets.h`, `panel.h`, `play_rows.h`, `screen.h`, `shape_editor.h`, `voice_master.h`, `voice_screen.h` | `filter_ui.h`, `fx_ui.h`, `lfo_ui.h`, `master_ui.h`, `osc_spec_page.h`, `osc_ui.h`, `voice_ui.h` | `ui_base_host`, `ui_led_host`, `ui_cov_steppers_lists_host`, `ui_cov_new_parts_host`, `ui_composite_host`, `ui_screen_host`, `ui_lfo_host`, `ui_fx_editor_host`, `ui_fx_tables_host` |
| 4 | **OSC selectors & panels (`UpDownStepper`, `PictogramSelector`, `TypeSelector`, `UnisonBox`, `WarpSlot`, `WarpPair`, `KnobRandPair`, `SourcePicker`, `BasicControls`, `DetailButton`, `RouteButton`)** (`base.h`, `composite.h`, `panel.h`) | **CONVERT AFTER / KEEP UNTIL OSC NATIVE LANDS** (Wait until `nativeOscDetailSupported` fallback removal completes; `UnisonBox`, `WarpSlot`, `BasicControls`, `TypeSelector` still run on OSC fallback when `nativeOscDetailSupported` is false) | `base.h`, `composite.h`, `panel.h`, `play_rows.h`, `screen.h`, `lfo_screen.h` | `osc_ui.h`, `osc_page.h`, `lfo_ui.h` | `ui_base_host`, `ui_composite_host`, `ui_screen_host`, `ui_cov_composites_panels_host`, `ui_cov_steppers_lists_host`, `ui_cov_toggles_buttons_host` |
| 5 | **Play selectors (`Carousel`, `UsedSelector`) & LFO parts (`RateControl`, `ShapePicker`, `SyncSwitch`)** (`carousel.h`, `used_selector.h`, `lfo_parts.h`) | **CONVERT AFTER NATIVE-GATING CHECK** (`Carousel` / `UsedSelector` still used by Play compact embeds before `nativeSelector_` / `LFO` full-screen page mode; `RateControl`/`ShapePicker`/`SyncSwitch` are DUP + Q model for LFO) | `carousel.h`, `used_selector.h`, `lfo_parts.h`, `env_screen.h`, `filter_display.h`, `lfo_screen.h` | `env_ui.h`, `filter_ui.h`, `lfo_ui.h`, `ui_bind.h` | `ui_carousel_host`, `ui_env_host`, `ui_lfo_host`, `ui_screen_host`, `ui_cov_steppers_lists_host` |
| 6 | **Mixer strip controls (`Fader`, `SliderBar`, `RouteDropdown`, `MixerStrip`)** (`mixer_strip.h`, `mixer_screen.h`) | **CONVERT TO MODEL** (`DUP + Q model`; Mixer strip faders/sliders/dest/power are layout + Q-Link model when `nativeBase()` is true; `listOpen_` fallback claim needs device touch check) | `mixer_strip.h`, `mixer_screen.h` | `mixer_ui.h` | `ui_cov_knobs_values_host` (`Fader`, `SliderBar`), `ui_cov_composites_panels_host`, `mixer_native_ui_host`, `mixfx_ui_host` |
| 7 | **Mod bars & source board (`HBar`, `ModSourceBoard`)** (`base.h`, `mod_board.h`) | **MIXED (`KEEP-UNCLEAR` / `LIVE-ONLY` split)**: `ModSourceBoard` in Mod/Env/Osc/Filter is DUP (`setNativeModBoard`), but **`PointModPage.board` (`lfo_ui.h`) is LIVE-ONLY**; `HBar` is `KEEP-UNCLEAR` on overlay-only `ModulationsPage`/`ModTargetsPage`/`ModMatrixView` | `base.h`, `mod_board.h`, `mod_matrix.h`, `mod_targets.h`, `panel.h`, `shape_editor.h` | `ui_bind.h`, `mod_ui.h`, `lfo_ui.h` | `ui_base_host`, `ui_cov_knobs_values_host`, `ui_composite_host`, `mod_ui_host` |
| 8 | **Action buttons (`MenuButton`)** (`base.h`) | **PARTIAL / KEEP FOR `LIVE-ONLY` PAGES**: DUP on native Filter/LFO/Env/Mod/FX/Osc buttons; **LIVE-ONLY** on curve/editor command bars (`ScalarCurveEditor`, `ShapeEditor`, non-native `PzCurveEditor`, Keyboard/Targets fallbacks) — cannot delete draw/touch until those command buttons have skin actions | `base.h`, `composite.h`, `env_screen.h`, `filter_display.h`, `fx_slot_editor.h`, `lfo_screen.h`, `mixer_strip.h`, `mod_detail.h`, `mod_matrix.h`, `mod_targets.h`, `panel.h`, `pz_curve_editor.h`, `scalar_curve_editor.h`, `screen.h`, `shape_editor.h` | `filter_ui.h`, `osc_spec_page.h`, `osc_ui.h` | `ui_base_host`, `ui_cov_toggles_buttons_host`, `ui_scalar_curve_host`, `ui_fx_editor_host`, `ui_lfo_host` |
| 9 | **`TextEntry`** (`text_entry.h`) | **KEEP (`LIVE-ONLY`)**: Macro rename (`macro_ui.h`) and LFO shape name (`lfo_ui.h`); blocked on host `TextEditOverlay` write-back path | `text_entry.h` | `lfo_ui.h`, `macro_ui.h` | `ui_text_entry_host` |
| 10 | **`ListPopup`** (`list.h`) | **KEEP-UNCLEAR**: Plugin-drawn chooser popups in 8 overlay pages (`env/fx/lfo/macro/mixer/mod/filter/osc`); host `MenuOverlay` migration open in `TODO.md` | `list.h`, `carousel.h`, `led_list.h`, `env_screen.h`, `used_selector.h` | `env_ui.h`, `filter_ui.h`, `fx_ui.h`, `lfo_ui.h`, `macro_ui.h`, `mixer_ui.h`, `mod_ui.h`, `osc_ui.h` | `ui_list_host`, `ui_cov_popups_scroll_host` |
| 11 | **Graphs, meters, displays, editors, indicators, QGroup** | **KEEP (`LIVE-ONLY` / Core)** — NOT generic control widgets: never convert away from `Component` / custom drawing | `display.h`, `env_graph.h`, `filter_display.h` (`FilterDisplay`), `fx_display.h`, `lfo_display.h`, `time_ruler.h`, `scalar_curve_editor.h` (canvas), `shape_editor.h` (`ShapeCanvas`), `pz_curve_editor.h` (`PzCurveCanvas`), `voice_grid.h`, `note_mask.h`, `clip_hold.h`, `screen.h` (`OscMixIndicator`), `qgroup.h` | `osc_display.h`, `osc_marks.h`, `spectrum_curve.h`, `native_marker_covers.h`, `page_detector.h`, `host_background.h`, `q_publish.h`, `q_publish_mod.h` | `ui_display_host`, `ui_env_host`, `ui_time_ruler_host`, `ui_voice_grid_host`, `spectrum_curve_host`, `display_host` |

### 3.2 Parallel worker disjointness

Once the foundation is merged, parallel workers can convert **page-local** or **subsystem-local** families concurrently without touching shared headers:

- **Worker A — Mixer strip family (`Fader`, `SliderBar`, `RouteDropdown`, `MixerStrip`)**:
  - Owns: `shell-mpc/ui/mixer_strip.h`, `shell-mpc/ui/mixer_screen.h`, `shell-mpc/overlay/mixer_ui.h`, `shell-mpc/ui/tests/ui_cov_composites_panels_host.cpp` (Mixer parts), `shell-mpc/overlay/tests/mixfx_ui_host.cpp` (Mixer parts). Disjoint from all other tabs!
- **Worker B — Toggle family (`ToggleIcon`, `LedToggle`, `StripPower`, `LedSwitchProxy`)**:
  - Owns: `shell-mpc/ui/led_toggle.h`, toggle logic in `env_screen.h`, `filter_display.h`, `voice_master.h`, `voice_screen.h`, `mod_detail.h`, `fx_slot_editor.h`, `lfo_screen.h`, `overlay/env_ui.h`, `overlay/filter_ui.h`, `overlay/voice_ui.h`, `overlay/master_ui.h`, and `ui/tests/ui_cov_toggles_buttons_host.cpp` + `ui_led_host.cpp`. Coordinate with Worker A only on `StripPower` (`mixer_strip.h`) and Worker C on `fx_slot_editor.h`/`lfo_screen.h`.
- **Worker C — Stepped chooser family (`Stepper`, `LedList`)**:
  - Owns: `shell-mpc/ui/led_list.h`, `shell-mpc/ui/lfo_parts.h`, `ui_cov_steppers_lists_host.cpp`, `ui_fx_tables_host.cpp`. Better serialized *after* or paired with Worker B for shared screen files (`fx_slot_editor.h`, `lfo_screen.h`, `voice_screen.h`, `voice_master.h`), OR split by *page domain* instead of widget class once `*Model` classes exist.
- **Better scaling rule for >2 workers**: extract all new `*Model` classes FIRST (Section 4), then shard page/screen/test cleanup **by subsystem** (Env/Filter, LFO, Osc, Mixer/FX, Master/Voice, Mod) — subsystem shards are almost completely disjoint across `ui/*.h`, `overlay/*_ui.h`, and `overlay/tests/*_ui_host.cpp`.
- **Keep untouched (`LIVE-ONLY` / `KEEP-UNCLEAR`)**: Do not schedule workers to delete `TextEntry`, `PointMod` `ModSourceBoard`, `ListPopup`, `HBar` (until overlay-only mod rows are replaced), or curve-editor `MenuButton`s until `TODO.md` host/skin prerequisites land.

---

## 4. Shared Headers (Single-Owner Lock List) & Edit-Last Merge Order

### 4.1 Single-owner shared headers

**ONLY ONE worker (the integrator) may edit these files directly.** Parallel family workers must **never** commit edits to them; instead, a worker adding a new model type during parallel work should define it in a dedicated header `shell-mpc/ui/model_<family>.h` (including `"model.h"`) and leave merging into shared headers to the integrator pass:

1. `shell-mpc/ui/model.h` — canonical model definitions (`ControlModel`, `KnobModel`, `NumberFieldModel`, etc.)
2. `shell-mpc/ui/base.h` — generic control widget definitions & `using` aliases
3. `shell-mpc/ui/core.h` — `Component` base, `Pen::control()`, `Cell`, `setHostPainted()` shim, `ModEntry::colour()`
4. `shell-mpc/ui/qgroup.h` — `QGroup` container & adjacency checks
5. `shell-mpc/ui/composite.h`, `shell-mpc/ui/panel.h`, `shell-mpc/ui/screen.h` — shared container composites (`Composite`, `KnobPanel`, `BasicControls`, `OscTopRow`, `OscillatorFullScreen`)
6. `shell-mpc/kit/binder.h`, `shell-mpc/kit/page_base.h` — binding & page dispatch machinery
7. `shell-mpc/ui/theme.h`, `shell-mpc/ui/theme_tokens.h` — theme look structs & generated tokens

*(Note: there is no `shell-mpc/ui/pen.h`; `Pen` lives inside `shell-mpc/ui/core.h`.)*

### 4.2 Exact edit-last merge order (integrator sequence)

After parallel family workers merge cleanly:

1. **Step 1 — Consolidate `*Model` definitions into `shell-mpc/ui/model.h`** (merging any temporary `model_<family>.h` headers, verifying standalone `c++ -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic -Werror -x c++-header shell-mpc/ui/model.h` with zero `-I` flags).
2. **Step 2 — Delete converted widget classes from `shell-mpc/ui/base.h`** (leaving aliases where needed) and strip generic control wrappers from `composite.h` / `panel.h` / `screen.h`.
3. **Step 3 — Clean up `qgroup.h`, `kit/binder.h`, and `kit/page_base.h`**: once zero generic control widgets remain as `Component` subclasses (only graphs/displays/indicators remain on `Component`), remove legacy `Component*` overloads (`QGroup::check(Component* const*, ...)`, `QGroup::drawMappedMembers(..., Component* const*, ...)`, `PageBase::bind(..., Component&, ...)` if displays are registered separately via `addPart`).
4. **Step 4 — Clean up `shell-mpc/ui/core.h`**: remove the `ui::setHostPainted(ControlModel*, bool)` compatibility shim and any control-only fields/methods on `Component`, leaving `Component` strictly for plugin-drawn graphs, meters, curve displays, animation, and indicators (`AGENTS.md` owner 2026-11-27).
5. **Step 5 — Last: delete `Pen::control` and unused control tokens** (`docs/review/BUTTON-MUTATION-AUDIT-2026-11-27.md` section C): once zero live callers of `Pen::control()` remain (after `LIVE-ONLY` curve-editor buttons, `TextEntry`, `ListPopup`, `ModSourceBoard` migrate to native skin controls), delete `Pen::control()` from `shell-mpc/ui/core.h:128` and remove unused control tokens (`panel0`, `panel1`, `rControl`, button border tokens) from `theme.h` / `theme_tokens.h` and `scripts/gen-ui-theme.py`.

---

## 5. Kept/Unclear Items & Observed Pre-Existing Issues

### 5.1 Residual metadata kept intentionally for page compatibility

During the `Knob`/`NumberField` proof, several non-drawing fields had to stay on `KnobModel` / `NumberFieldModel` because existing `overlay/*_ui.h` or layout/contract code still reads them:

- `KnobModel::kLongPressMs` (`600.0`): read by `overlay/env_ui.h:438`.
- `KnobModel::State::isDb`, `dbMin`, `dbMax`, `skew`: populated by `overlay/master_ui.h:353` and `mixer_ui.h` for dB conversion.
- `KnobModel::State::Status` (`Active, Off, Inactive, Disabled`), `hasStatus()`, `statusString()`, `modulated()`, `tab()`: read by `KnobModel::formatText()` and `overlay/osc_ui.h:2455`.
- `KnobModel::State::compact`, `side`, `small`, `large`, `dialMax`: preserved on `KnobModel::State` because `minSize()` depends on `compact`/`small`/`side`, and screen/test assertions inspect `state.large`/`state.side`/`state.dialMax`.
- `NumberFieldModel::State::pxPerStep`: still assigned by `overlay/osc_ui.h:1629`, `overlay/voice_ui.h:315`, and `ui/voice_screen.h:61`.
- `NumberFieldModel::maxBox`: checked by `ui/fx_slot_editor.h` and `overlay/tests/mixfx_ui_host.cpp:1757`.
- `ModEntry::colour()` declaration in `model.h`, inline definition in `core.h`: called by `overlay/tests/macro_names_ui_host.cpp:190` without dragging `theme.h` into `model.h`.
- `ui::setHostPainted(ControlModel*, bool)` shim in `core.h`: required by `overlay/lfo_ui.h` and `overlay/osc_ui.h` while `Binder::Item::c` holds mixed widgets + models.

### 5.2 Pre-existing baseline failures / unrelated test issues on `2d59b1d9`

Verified by archiving and running base commit `2d59b1d9` directly in `/tmp` so they are not misattributed to the `ControlModel` refactor:

- `ui_display_host`: **1 pre-existing failure** at line 311 (`QGroup: the frame can run around cards (a rectangle given by the owner), inside the rectangle`) — both on `2d59b1d9` and after Batch 3.
- `ui_screen_host`: **29 pre-existing failures** on `2d59b1d9` (exact 29-for-29 parity after Batch 3, 0 new failures), mainly around Noise / Sub / Sample / Spectral `OscillatorFullScreen` counts/groups/`displayRect`/`MORE`/OFF mix indicator assertions that diverged in earlier native-controls/bold-cut merges.
- `ui_fx_editor_host.cpp:635`: calls `fs.setPeekInset(64)` which does not exist on `FxFullScreen` at `2d59b1d9`.
- `ui_lfo_host.cpp:401,403`: accesses `f.instance` on `LfoFullScreen` which was already absent at `2d59b1d9` (`instance` moved/removed from `LfoFullScreen`).
- `ui_scalar_curve_host.cpp` and `*_render_fixture.cpp`: standalone `-fsyntax-only` without the repo root `-I .` / kit include flags hits missing `'voice/own_content.h'` / `'shell-mpc/native_*.h'` (they require the full `accept.sh`/CMake include set).
