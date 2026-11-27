# The UI component library of the SynthyBoy MPC plugin

For developers and AI coding agents who want to build their own plugin-drawn pages (and later their own Akai MPC plugin UI) from these components. Start here; the details are one page per component.

[Képes komponenskatalógus (magyar)](kepes-katalogus.html)

## A képes katalógus karbantartása

A képes katalógus kizárólag az önálló MPC host natív skin-komponenseit mutatja. C++ overlay-vezérlő, kézzel utánrajzolt komponens vagy külön HTML/SVG/CSS-animáció nem kerülhet bele. Kép csak a használt natív komponensdefinícióból vagy annak tényleges skin-eszközéből, illetve az MPC hardverről származhat; eredetét meg kell nevezni. A teljes C++ könyvtár alábbi dokumentációja külön fejlesztői referencia.

### Tárcsák (Pan, Level): szöveg nélküli méretváltozatok

A Pan és a Level kártyája a `components/native/<név>/catalog.json` `dial` bejegyzéséből készül: csak a tárcsa rajza, felirat és érték nélkül, az `ui::Knob`-recept (`level.render_dial_frame`) szerint, minden változat a valós méretén (1:1, nincs átméretezés). A `level.render_dial_preview` a feliratot paraméterrel kapcsolja ki, nem vágással. A méretfeliratot (`Normál · 90 × 90 px`) a HTML adja, nem a kép.

Új méret felvétele: egy elem a `dial.variants` listába (`id`, `label`, `diameter` = `components/sizes.json` egész méret-token, `file`, `alt`), majd `python3 -c "import sys; sys.path[:0]=['scripts','.']; import render_native_catalog_cards as r; r.render_dial_cards()"` (a képek), végül a fenti HTML-generálás. A `state`-ek (`dial.states`) és az állapotok közti távolság (`dial.state_gap`) közösek; az új változat a `scripts/tests/test_native_dial_catalog.py` ellenőrzéseit automatikusan megkapja. A skin filmstripjeit ez nem érinti.

Ugyanez a bejegyzés adja az állapotképeket (`dial.state_images`: állapotonként egy szöveg nélküli kép, normál méretben) és a szöveges változatokat (`dial.text`: `caption`, `value`, `state`, `diameter` és `variants`). Elrendezés (`layout`): `right` (felirat jobbra, alatta az érték), `bottom` (csak az érték a tárcsa alatt), `top` (felirat fent, érték lent). Új elrendezés: új elem a `dial.text.variants`-ba, és az elrendezés felvétele a `level.DIAL_TEXT_LAYOUTS`-ba és a `render_dial_labelled`-be. A felirat és az érték mindig ugyanazzal a szövegstílussal készül: `level.DIAL_TEXT_STYLE` (`font.value`, `text.value`), egyetlen helyen átállítható. A `render_dial_labelled(..., disabled=True)` a letiltott változatot adja (inaktív tárcsa, tompított szöveg); alapból ki van kapcsolva.

### Sorrend, Label, On/Off label, legördülő-változatok

A katalógus sorrendje a `components/native/catalog-order.json` adata (`first`: Label, On/Off label, Pan, Level, Button, Selector; `last`: Header; a többi a `catalog.json` `order` mezője szerint). Részletek: [label.md](label.md), [on_off_label.md](on_off_label.md). A Selector `dropdown` bejegyzése három elrendezést mutat egymás mellett (`selector.render_dropdown_variant`: `placement` none|above|inside, `align` center|left); képek: `r.render_dropdown_cards()`. A komponens-modul a bejegyzését a `catalog_sections(entry)` függvénnyel adja a generátornak (kulcsok: `label`, `on_off`, `dropdown`, `header`).

### Header (javaslat v0)

A `components/native/header/` bejegyzés a fejléc-sor nyolc példáját mutatja, csak katalógusban: [header.md](header.md).

A HTML generált fájl; közvetlenül ne szerkeszd. Újragenerálás a repó gyökeréből:

```sh
python3 scripts/gen-native-component-catalog.py
```

A commit előtti Git-hook automatikusan újragenerálja és a commitba teszi a HTML-t, ha a komponenskönyvtár, a generátor vagy a sablon változik. Kizárólag a stage-elt fájlokból dolgozik. Ebben a checkoutban engedélyezve van; új klónban egyszer állítsd be:

```sh
git config core.hooksPath .githooks
```

A futtatáshoz Python 3 és Pillow szükséges. Hibás metaadat vagy hiányzó kép esetén a commit leáll. Új, elfogadott komponens a saját `catalog.json` fájljával automatikusan bekerül; üres könyvtár nem jelenik meg.

Minden jóváhagyott natív komponens a saját `components/native/<név>/catalog.json` fájljában választja ki a saját `img/` könyvtárából bemutatandó képeket. A generátor a valódi `<név>/<név>.py` modullal rendelkező könyvtárakat fedezi fel; szimbolikus alias és régi segédkönyvtár nem kerül be. Új komponenshez elegendő a saját modul, képek és metaadat: nincs központi komponenslista.

A metaadat `version: 1`, `approved: true`, egész `order`, `title` és `sections` mezőket tartalmaz. A szakasz opcionális `heading` feliratot és `images` listát kap. Egy képnél `file` (az `img/` alatti relatív út) és `alt` kötelező; `caption` és `presentation` (`sample` vagy `hero`) opcionális. A `sample` természetes méretű minta, a `hero` áttekintő kép. A méretet a képfájlból olvassa ki; csak túlmintavételezett kép natív méretre kicsinyítéséhez adj `display_size: "mérettoken.neve"` hivatkozást a közös méretfájlban megadott `[szélesség, magasság]` párra, az oldalarány megtartásával. Az elutasított összehasonlítások és a filmstripek nem szerepelnek a metaadatban, ezért nem jelennek meg. Hiányzó kép, hibás metaadat és az adott `img/` mappából kilépő útvonal hibával leállítja a generálást.

A színek egyetlen adatforrása `components/colors.json`, a méreteké és elrendezéseké `components/sizes.json`; ezeket a natív Python-komponensek és a katalógus egyaránt a `components/style.py` betöltőn keresztül használják. Egy méret másikra hivatkozhat `{"ref": "height.medium"}` alakban, így a natív geometriában és a feliratokban ugyanaz a szerep változik. A metaadatban a `{size:height.medium}` méretértéket, a `{size_label:height.medium}` a közös méretnevet illeszti be. A HTML-sablon (`scripts/templates/native-component-catalog.html`) szín- és mérettokeneket használ, nincs benne másolt paletta.

A HTML-generátor a kiválasztott kész képeket olvassa, nem rajzol helyettük mintát. A stílusértékek jelenlegi átvezetése megtartja az elfogadott képek értékeit. Későbbi stílusváltozáskor az érintett képeket először a valódi komponensrajzolóval kell frissíteni (a natív kártyákhoz `scripts/render_native_catalog_cards.py`, a működő OFF állapotokhoz a komponens saját natív állapotrajzolója); ezután a fenti egyetlen HTML-generálási parancs frissíti a katalógust. Régi összehasonlító képet ne válassz be automatikusan.

Hordozható terjesztéskor a komponens saját modulja és `img/` mappája mellé a közös `components/style.py`, `colors.json`, `sizes.json` fájlokat is csomagold, a repóval azonos `components/native/<név>/` elrendezésben. A más natív komponenseket használó összetett elemekhez a használt komponensmodulokat és azok képeit is csomagold. A publikus natív komponens-API-k újrahasználhatók; megvalósításukat és a közös stílusadatokat ne másold az összetett elembe.

## What it is
`shell-mpc/ui/` is a header-only C++17 library, namespace `synthyboy_mpc::ui`. `ui.h` includes most of it (43 headers and the umbrella `ui.h`). It draws with the scene ops of `shell-mpc/overlay/gpu/scene.h` (a GPU helper draws them on the MPC's display plane; `shell-mpc/overlay/scene_cpu.h` is the CPU renderer with the same meaning, the fallback and the test reference). Pictures of every page: `docs/user-guide/img/*.png`. The design record is docs/MPC.md "UI components". Licence: AGPL-3.0-or-later ([REUSE.md](REUSE.md)).

## The one contract every component follows
1. State in: a plain struct (`component.state`) holds what it shows. The component never stores the truth.
2. A rectangle in: `place (Rect)`. The component draws only inside it.
3. Draw: `draw (gpu::SceneBuilder&)` emits scene ops. No allocation, no locks, no I/O; the same state gives the same ops.
4. Input in: `touchDown (x, y, nowMs)`, `touchMove (x, y)`, `touchUp (x, y)`, `tick (nowMs)` (call it while a finger is down), `turn (delta)` (a hardware knob), `wheel (notches)`. Single finger, pixels. A gesture that went down inside the rectangle is kept until the touch up.
5. Change out: every call returns `Change { kind, id, value, index }`. `kind` `None` means nothing happened. Kinds: `Value` (0 to 1), `Index` (a whole number: list position, switch 0 / 1, steps of a number field), `Request` (open a menu, page or popup), `Depth`, `ModsRequest`, `AddRequest` (modulation).
6. The owner writes values back. You write the parameter, and on the next update you put the new value into `state`. A change that would not change the value is not reported.
7. `id` is an opaque integer you gave with `setId`. The component never knows what a parameter is.
8. Touch targets are at least 56 px in the smaller direction. `minSize()` is the smallest legible size; the layout must give at least that (`sizeOk()`).
9. Q groups and hardware mapping: Up to four members form a `QGroup` for hardware knob routing. Functional hardware mappings and meaningful parameter values are preserved. VISUAL RULE (owner, 2026-11-24): NO visible Q-Link numbers, badges, or dedicated number lanes on our components, screen designs or previews. Keep 48 px total height on 48 px blocks without dedicated number lanes. Details: [qgroup.md](qgroup.md).
10. The theme: every colour, size and line width comes from `theme::L()` ([theme.md](theme.md)); never write a colour or a size literal in a component.
11. Rules of the look: frameless LED controls instead of boxed toggles, one control per value, text must fit its box, no third-party product names in shown text.

## A knob on a page
```cpp
#include "shell-mpc/ui/ui.h"
using namespace synthyboy_mpc::ui;

class MyPage
{
public:
    enum Id { kCutoff = 1 };
    double written = 0.5;                                    // stands for the parameter (the truth)

    MyPage()
    {
        cutoff.setId (kCutoff);                              // declare: an opaque id
        cutoff.state.defaultValue = 0.5;                     // a double tap sets this
        cutoff.place ({ 100, 120, 120, 160 });               // place: the rectangle (>= minSize 100 x 72)
    }
    void update()                                            // set state: every update, from the parameter
    {
        cutoff.state.value = cutoff.state.modValue = written;
        std::snprintf (cutoff.state.valueText, sizeof cutoff.state.valueText, "%.0f%%", written * 100.0);
    }
    void draw (gpu::SceneBuilder& sb) const { cutoff.draw (sb); }          // draw
    void down (int x, int y, double nowMs) { handle (cutoff.touchDown (x, y, nowMs)); }   // route touch
    void move (int x, int y)               { handle (cutoff.touchMove (x, y)); }
    void up   (int x, int y)               { handle (cutoff.touchUp (x, y)); }
    void tick (double nowMs)               { handle (cutoff.tick (nowMs)); }

private:
    void handle (const Change& c)                            // handle the Change
    {
        if (c.kind == ChangeKind::Value && c.id == kCutoff)
            written = c.value;                               // write the parameter; update() shows it
    }
    Knob cutoff { "CUTOFF" };
};
```
A scene is built with `gpu::SceneData data;` (heap: about 336 KB) and `gpu::SceneBuilder sb (data); sb.begin (gpu::kKindUi, 0, 1280, 800);` as in `shell-mpc/ui/tests/ui_test.h`. On the device the overlay does that for you ([PAGES.md](PAGES.md)).

## Which component for which job
| Job | Use | Page |
|---|---|---|
| on / off | `LedToggle` | [toggle.md](toggle.md) |
| 2 to 8 named choices, all visible | `LedList` | [led-list.md](led-list.md) |
| more than 8 choices, or no room | `Stepper` with `listable` + `ListPopup` | [stepper.md](stepper.md), [list-popup.md](list-popup.md) |
| a short list of named entries stepped through often (the selector of a Play third) | `Carousel` + `ListPopup` | [carousel.md](carousel.md) |
| the Env / LFO item selector of the merged tabs (used items + selected + one `+`) | `UsedSelector` | [used-selector.md](used-selector.md) |
| the NOTE / VELO transfer curve (graph, points, legato, used-by list) | `ScalarCurveEditor` | [scalar-curve-editor.md](scalar-curve-editor.md) |
| choice that is better seen than read | `PictogramSelector` | [pictogram-selector.md](pictogram-selector.md) |
| continuous value 0 to 1 | `Knob` | [knob.md](knob.md) |
| continuous value in a row or cell | `SliderBar`, `HBar` | [bars.md](bars.md) |
| level with a peak meter | `Fader` | [bars.md](bars.md) |
| whole number, or decimal with a step | `NumberField` | [number-field.md](number-field.md) |
| small integer (voices 1 to 16) | `UpDownStepper` | [stepper.md](stepper.md) |
| set of notes of an octave | `NoteMask` | [note-mask.md](note-mask.md) |
| open a page or a popup | `MenuButton`, `DetailButton` | [buttons.md](buttons.md) |
| name something | `TextEntry` | [text-entry.md](text-entry.md) |
| pick a modulation source | `ModSourceBoard` | [mod-source-board.md](mod-source-board.md) |
| the modulations of one control | `ModulationsPage` | [modulations-page.md](modulations-page.md) |
| the targets of one source | `ModTargetsPage` | [modulations-page.md](modulations-page.md) |
| four values on four hardware knobs | `QGroup` | [qgroup.md](qgroup.md) |
| a grid of knobs from a table | `KnobPanel` | [knob-panel.md](knob-panel.md) |
| a page of details with a picture | `DetailPage` | [knob-panel.md](knob-panel.md) |
| an envelope you edit by touch | `EnvGraph` | [env-graph.md](env-graph.md) |
| a drawn LFO shape | `ShapeEditorPage` | [shape-editor.md](shape-editor.md) |
| a filter response with a handle | `FilterDisplay` | [filter-display.md](filter-display.md) |
| an oscillator or LFO picture | `display::draw`, `lfodisplay::draw` | [display.md](display.md) |
| a chain of effects | `FxChainBar`, `FxSlotEditor` | [fx-chain.md](fx-chain.md), [fx-editor.md](fx-editor.md) |
| your own widget | derive from `Component` / `Composite` | [core.md](core.md), [composites.md](composites.md) |

## The components
**Basic controls**: [Knob](knob.md), [NumberField](number-field.md), [Stepper and UpDownStepper](stepper.md), [Carousel and EnvCarousel](carousel.md), [UsedSelector](used-selector.md), [ScalarCurveEditor](scalar-curve-editor.md), [LedToggle and ToggleIcon](toggle.md), [LedList](led-list.md), [MenuButton, DetailButton, RouteButton, RouteDropdown](buttons.md), [HBar, SliderBar, Fader](bars.md), [PictogramSelector](pictogram-selector.md), [NoteMask](note-mask.md).

**Lists and pop-ups**: [ListPopup](list-popup.md), [TextEntry](text-entry.md), [ModSourceBoard](mod-source-board.md), [FxGallery](fx-gallery.md) (engine-bound), [ModulationsPage and ModTargetsPage](modulations-page.md).

**Displays**: [display::draw and lfodisplay::draw](display.md), [EnvGraph](env-graph.md), [FilterDisplay](filter-display.md), [FxDisplay](fx-display.md) (engine-bound), [Master and Voice displays](voice-displays.md), SpectrumStrip in [macros.md](macros.md), the shape canvas in [shape-editor.md](shape-editor.md).

**Composite components and Q groups**: [Composite and the small composites](composites.md), [BasicControls](type-bar.md), [KnobPanel and DetailPage](knob-panel.md), [QGroup](qgroup.md), [ShapePicker, SyncSwitch, RateControl, InstanceBar](lfo-parts.md), [FxSlotCard, FxChainBar, FxBandBoard](fx-chain.md) (engine-bound).

**Composite screens**: [OscillatorCompact and OscillatorFullScreen](osc-screens.md), [EnvCompact and EnvFullScreen](env-screens.md), [LfoCompact and LfoFullScreen](lfo-screens.md), [FilterFullScreen](filter-screens.md), [MixerFullScreen](mixer.md), [ModMatrixView and ModSlotDetail](mod-screens.md), [MacroView](macros.md), [MasterScreen](master-screen.md), [VoiceScreen and VoiceGrid](voice-screen.md), [ShapeEditorPage](shape-editor.md), [FxFullScreen and FxSlotEditor](fx-editor.md) (engine-bound).

**Helpers**: [core.h (Rect, Change, Component, Pen, Cell, TapGesture)](core.md), [theme.h](theme.md), [choice_names.h, play_rows.h, time_ruler.h, lfo_model.h, layoutRow, ui.h](helpers.md).

## Guides
- [PAGES.md](PAGES.md): how a page is put together: `PageView`, the Ui class, binding to parameters, Q groups and the 16 slots, the stack, live drawing, cached cells, the layout lint, checklists for a new page and a new control.
- [REUSE.md](REUSE.md): what to lift into another plugin, what is bound to SynthyBoy, the overlay runtime, the licence, and what is not yet a template.
- `shell-mpc/ui/AGENTS.md`: instructions for AI coding agents working on this library.

## Tests and verification
`shell-mpc/ui/tests/` (`ui_base_host.cpp`, `ui_composite_host.cpp`, `ui_screen_host.cpp`, the `ui_cov_*` files, the per-screen host tests, `ui_demo.cpp`; inventory in `shell-mpc/ui/tests/COVERAGE.md`) and `shell-mpc/overlay/tests/` (page host tests, `coverage_host.cpp`, `qslots_host.cpp`, `layout_lint_host.cpp`, the gallery renderers). Built and run by the integration session (`shell-mpc/accept.sh`, under AddressSanitizer and UBSan), not by an individual worker.

### Natív Selector és ikongomb

A natív komponensek forrása az [inMusic MPC Plugin Builder SDK UI-referenciája](https://github.com/inmusic-software/mpc-plugin-builder-sdk/blob/main/docs/ui-reference.md). A SynthyBoy Selector paraméterei: opcionális ikon, a fő értéket adó title és opcionális második felirat (name). A lefelé mutató nyíl beépített. Az egysoros felület 48 px, a kétsoros 56 px magas, és a rendelkezésre álló cellában függőlegesen középre kerül. A Power és Expand ikon gombfelülete 48 px magas a meglévő 64 × 56 px-es érintési cellában; a galéria képaláírása a cellán kívül található.

#### Címkézett Selector összetett gyártó (Titled Selector Factory)

A [`components/native/selector/selector.py`](../../components/native/selector/selector.py) modulban elérhető `build_titled_selector` (alias: `titled_selector_definition`) egy felső feliratcímkét és egy natív választómenüt foglal egybe.

- **Hordozható függőségek:** [`components/native/selector/selector.py`](../../components/native/selector/selector.py), [`components/style.py`](../../components/style.py), [`components/colors.json`](../../components/colors.json), [`components/sizes.json`](../../components/sizes.json).
- **Méretszerződés:** Alapértelmezett méret a stílustokenekből származtatott `TITLED_SELECTOR_SIZE = (control.width, height.high + active_text.default.height) = (202, 80)`. A fogadó képernyő (pl. Voice LFO MODE) átadhat egyedi méretet is (pl. `368 × 80 px`).
- **Érvényesítés:**
  - Külső méretek (`width > 0`, `total_height > 0`) és címkemagasság (`title_height > 0`) szigorúan pozitívak.
  - A belső választómagasság legalább `height.high` (56 px), és a címkével együtt nem haladja meg a teljes magasságot.
  - Csak `Absolute` elhelyezés támogatott: arányos (`Proportional`) bounds esetén a factory explicit `ValueError`-t dob a hibás vizuális fedés elkerülésére.
- **Állapotkötés:**
  - A külső feliratot a `caption_handle` (alapértelmezés: `"Caption"`) vezérli, vagy explicit `title_text` esetén fix szöveg.
  - A belső értéket a `title` handle vezérli (alapértelmezés: `"Data"`).
  - A felugró menüt a hostoverlay actionjei vezérlik (`Show Overlay`, pl. `popup="fx editor choice menu"`).

```python
from components.native.selector.selector import build_titled_selector

# Címkézett választó regisztrációja a skinben
choice_def = build_titled_selector(
    None,
    popup="fx editor choice menu",
    slot=(368, 80),
    skins_dir=skins_folder,
    caption_handle="Caption",
    play_face=True,
)
```
