# The native component library of the SynthyBoy MPC plugin

The controls of the plugin are the shared **native components**: the catalogue (`components/native/<name>`, the picture catalogue below), the tokens (`components/colors.json`, `components/sizes.json`) and the component recipes (`components/native`; the skin itself, `skin/`, is edited directly). Every control on every screen comes from its component. The overlay display layer (`shell-mpc/ui`, `shell-mpc/overlay`) holds live graphs, meters, curve, waveform and spectrum displays, animation, the automation indicators around native controls, the interaction of the curve editors, the Q-Link/parameter model and the layout of the native cells.

[Képes komponenskatalógus (magyar)](kepes-katalogus.html)

## A képes katalógus karbantartása

A képes katalógus kizárólag az önálló MPC host natív skin-komponenseit mutatja. C++ overlay-vezérlő, kézzel utánrajzolt komponens vagy külön HTML/SVG/CSS-animáció nem kerülhet bele. Kép csak a használt natív komponensdefinícióból vagy annak tényleges skin-eszközéből, illetve az MPC hardverről származhat; eredetét meg kell nevezni. Az alábbi C++ osztálydokumentáció a plugin-rajzolt réteg belső fejlesztői referenciája, nem külső könyvtár.

### Tárcsák (Pan, Level): szöveg nélküli méretváltozatok

A Pan és a Level kártyája a `components/native/<név>/catalog.json` `dial` bejegyzéséből készül: csak a tárcsa rajza, felirat és érték nélkül, az `ui::Knob`-recept (`level.render_dial_frame`) szerint, minden változat a valós méretén (1:1, nincs átméretezés). A `level.render_dial_preview` a feliratot paraméterrel kapcsolja ki, nem vágással. A méretfeliratot (`Normál · 90 × 90 px`) a HTML adja, nem a kép.

Új méret felvétele: egy elem a `dial.variants` listába (`id`, `label`, `diameter` = `components/sizes.json` egész méret-token, `file`, `alt`), majd `python3 scripts/render_native_catalog_cards.py` (a képek), végül a katalógus HTML kézi szerkesztése. A `state`-ek (`dial.states`) és az állapotok közti távolság (`dial.state_gap`) közösek; az új változat a `scripts/tests/test_native_dial_catalog.py` ellenőrzéseit automatikusan megkapja. A skin filmstripjeit ez nem érinti.

Ugyanez a bejegyzés adja az állapotképeket (`dial.state_images`: állapotonként egy szöveg nélküli kép, normál méretben) és a szöveges változatokat (`dial.text`: `caption`, `value`, `state`, `diameter` és `variants`). Elrendezés (`layout`): `right` (felirat jobbra, alatta az érték), `bottom` (csak az érték a tárcsa alatt), `top` (felirat fent, érték lent). Új elrendezés: új elem a `dial.text.variants`-ba, és az elrendezés felvétele a `level.DIAL_TEXT_LAYOUTS`-ba és a `render_dial_labelled`-be. A felirat és az érték mindig ugyanazzal a szövegstílussal készül: `level.DIAL_TEXT_STYLE` (`font.value`, `text.value`), egyetlen helyen átállítható. A `render_dial_labelled(..., disabled=True)` a letiltott változatot adja (inaktív tárcsa, tompított szöveg); alapból ki van kapcsolva.

### Sorrend, Label, On/Off label, legördülő-változatok

A katalógus sorrendje a `components/native/catalog-order.json` adata (`first`: Label, On/Off label, Pan, Level, Button, Selector; `last`: Header; a többi a `catalog.json` `order` mezője szerint). Részletek: [label.md](label.md), [on_off_label.md](on_off_label.md). A Selector `dropdown` bejegyzése három elrendezést mutat egymás mellett (`selector.render_dropdown_variant`: `placement` none|above|inside, `align` center|left); képek: `r.render_dropdown_cards()`. A komponens-modul a bejegyzését a `catalog_sections(entry)` függvénnyel adja a generátornak (kulcsok: `label`, `on_off`, `dropdown`, `header`).

### Button, IconButton

Kompakt választó család (RadioList, RadioGrid, SlideSwitch, RotarySelector; a régi RadioButton kivezetésre): [radio_family.md](radio_family.md).

Régi külsejű szöveges és ikonos gomb 32/40/48/56 px-en, ghost és disabled állapottal: [button.md](button.md), [icon_button.md](icon_button.md).

### FilterSelector

A Filter-oldali lánc-gomb (56 × 48 px, két lámpaszegmenssel) külön komponens, nem Selector: [filter_selector.md](filter_selector.md).

### Header (javaslat v0)

A `components/native/header/` bejegyzés a fejléc-sor nyolc példáját mutatja, csak katalógusban: [header.md](header.md).

A HTML generált fájl; közvetlenül ne szerkeszd. Újragenerálás a repó gyökeréből:

The catalogue pictures are redrawn with the component's own recipe on explicit command (output committed); the catalogue page `docs/ui-components/kepes-katalogus.html` is hand-maintained: edit it directly, there is no generator.

Nincs pre-commit hook és nincs generátor: a katalógus-HTML-t kézzel szerkesztjük.

Minden jóváhagyott natív komponens a saját `components/native/<név>/catalog.json` fájljában választja ki a saját `img/` könyvtárából bemutatandó képeket. A generátor a valódi `<név>/<név>.py` modullal rendelkező könyvtárakat fedezi fel; szimbolikus alias és régi segédkönyvtár nem kerül be. Új komponenshez elegendő a saját modul, képek és metaadat: nincs központi komponenslista.

A metaadat `version: 1`, `approved: true`, egész `order`, `title` és `sections` mezőket tartalmaz. A szakasz opcionális `heading` feliratot és `images` listát kap. Egy képnél `file` (az `img/` alatti relatív út) és `alt` kötelező; `caption` és `presentation` (`sample` vagy `hero`) opcionális. A `sample` természetes méretű minta, a `hero` áttekintő kép. A méretet a képfájlból olvassa ki; csak túlmintavételezett kép natív méretre kicsinyítéséhez adj `display_size: "mérettoken.neve"` hivatkozást a közös méretfájlban megadott `[szélesség, magasság]` párra, az oldalarány megtartásával. Az elutasított összehasonlítások és a filmstripek nem szerepelnek a metaadatban, ezért nem jelennek meg. Hiányzó kép, hibás metaadat és az adott `img/` mappából kilépő útvonal hibával leállítja a generálást.

A színek egyetlen adatforrása `components/colors.json`, a méreteké és elrendezéseké `components/sizes.json`; ezeket a natív Python-komponensek és a katalógus egyaránt a `components/style.py` betöltőn keresztül használják. Egy méret másikra hivatkozhat `{"ref": "height.medium"}` alakban, így a natív geometriában és a feliratokban ugyanaz a szerep változik. A metaadatban a `{size:height.medium}` méretértéket, a `{size_label:height.medium}` a közös méretnevet illeszti be. A HTML-sablon (`scripts/templates/native-component-catalog.html`) szín- és mérettokeneket használ, nincs benne másolt paletta.

A katalógus HTML a kiválasztott kész képeket mutatja, nem rajzol helyettük mintát. A stílusértékek jelenlegi átvezetése megtartja az elfogadott képek értékeit. Későbbi stílusváltozáskor az érintett képeket először a valódi komponensrajzolóval kell frissíteni (a natív kártyákhoz `scripts/render_native_catalog_cards.py`, a működő OFF állapotokhoz a komponens saját natív állapotrajzolója); ezután a katalógus HTML-t kézzel frissítsd. Régi összehasonlító képet ne válassz be automatikusan.

Hordozható terjesztéskor a komponens saját modulja és `img/` mappája mellé a közös `components/style.py`, `colors.json`, `sizes.json` fájlokat is csomagold, a repóval azonos `components/native/<név>/` elrendezésben. A más natív komponenseket használó összetett elemekhez a használt komponensmodulokat és azok képeit is csomagold. A publikus natív komponens-API-k újrahasználhatók; megvalósításukat és a közös stílusadatokat ne másold az összetett elembe.

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
