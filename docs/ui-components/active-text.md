# ActiveText (Állapotszöveg)

Az **ActiveText** ([components/native/active_text/active_text.py](../../components/native/active_text/active_text.py)) az owner által kifejezetten kért natív komponens, amely egy sima szöveges felirattal jeleníti meg a be- és kikapcsolt (aktív és inaktív) állapotot pusztán a szín változtatásával.

| Bekapcsolva (ON) | Kikapcsolva (OFF) |
| --- | --- |
| ![ActiveText ON](../../components/native/active_text/img/active-text-on.png) | ![ActiveText OFF](../../components/native/active_text/img/active-text-off.png) |

## Vizuális és stíluskövetelmények

- **Változatlan felirat**: a szöveg tartalma nem változik az állapotváltás során.
- **Szín jelzi az állapotot**:
  - **ON (aktív)**: közös aktív állapotszín (`active_text.on`, `#2ee6f0`).
  - **OFF (inaktív)**: inaktív szürke (`active_text.off`, `#384252`).
- **Egyetlen stílusforrás**: `components/colors.json`, `components/sizes.json` és `components/style.py`. Nincsenek beégetett hexadecimális kódok vagy képpontméretek.
- **Izoláció**: más natív komponensből nem importál; önálló csomagként terjeszthető és a család szintű stílusbetöltővel fut.

## Egyetlen Active állapotbemenet

A komponens kifelé irányuló állapotszerződése szándékosan minimális:
- **EGYETLEN állapotbemenet**: `active` (on/off, boolean / 0 vagy 1).
- **Nincs felesleges állapotparaméter**: nincs külön `enabled`, `selected`, `power`, `colour`, `captionState` vagy külső színpaletta-vezérlő.
- **Konfiguráció vs. állapot**: a felirat (`text`) és az elhelyezkedés/méret (`rect`, `width`, `height`) szerkesztési konfiguráció, nem második állapot.
- **Nem saját igazságtár**: a komponens nem tart belső aktív állapotot vagy toggle-logikát; az állapot igazságtára mindig a fogadó képernyő és a host marad.
- **Natív state handle**: a natív MPC adatmodellben a publikált állapot a `handleName: "Active"` vezérlőt követi.

A szöveg alap vizuális mérete 50 × 24 px. Ha érintéssel kapcsolható feliratként használjuk, a fogadó képernyő legalább 56 px-es érintési területet ad köré; a megjelenített állapot továbbra is kizárólag az `Active` bemenetből jön.

## Play SMOOTH bekötés

A komponens önállóan elérhető. A Play SMOOTH felirat és a meglévő saját poti összekötése külön folyamatban lévő munka; a jelenlegi pill kapcsoló leváltását ez az önálló komponenscommit még nem tartalmazza.

---

## Összetett címkézett csoportgyártó (Titled ActiveText Group Factory)

A [`components/native/active_text/active_text.py`](../../components/native/active_text/active_text.py) modul az önálló nyomógombok mellett egy additív csoportos gyártófüggvényt biztosít, amellyel egy külső felirat (title) és tetszőleges számú, egymástól független ActiveText kapcsoló fogható össze egyetlen natív TUI definícióba.

### Hordozható függőségek

- Forrásmodul: [`components/native/active_text/active_text.py`](../../components/native/active_text/active_text.py)
- Stílusbetöltő és tokenek: [`components/style.py`](../../components/style.py), [`components/colors.json`](../../components/colors.json), [`components/sizes.json`](../../components/sizes.json)
- Grafikai eszközök: `skins_dir` paraméteren keresztül átadott könyvtár, ahová a kapcsolók PNG képei generálódnak.
- Hibakezelés: ha a gyermekgombok képei nincsenek előre megadva és `skins_dir` értéke `None`, a factory nem ad vissza érvénytelen fájlhivatkozást, hanem explicit `ValueError`-t dob.

### Szerződés és állapotkötés

- **Passzív konténer:** a csoportos definíció passzív (`actions: []`, nem tartalmaz szülői `ToggleSwitch` akciót), és a konténer `ignoreMousePresses: True` beállítással jön létre, így az érintések közvetlenül a belső gyermekgombokhoz jutnak el.
- **Független gombállapotok:** minden gyermekgomb saját dedikált handle-t kap (`Active0`, `Active1`, vagy egyedi, pl. `ActiveA`), így több gomb nem osztozik közös paraméteren.
- **Érintési méret és átfedésvédelem:**
  - Minden gyermekgomb szélessége és magassága legalább `height.high` (56 px).
  - A gombok koordinátái pozitívak (`x >= 0`, `y >= 0`), és a megadott csoportméreten belül maradnak.
  - A factory páronként ellenőrzi az összes gombot: átfedő téglalapok esetén `ValueError`-t dob.
  - A gyermekgombok nem fedhetik át a címke téglalapját (`title_rect`).
- **Ütközésmentes eszközfájlnevek:** a felirat, méret és betűméret alapján számított SHA-1 lenyomat (`<prefix>_<stem>_<digest>_on.png`) garantálja, hogy azonos feliratú, de eltérő méretű kapcsolók nem írják felül egymás képeit.

### Használati példa

```python
from components.native.active_text.active_text import (
    build_titled_active_text_group_definition,
    titled_active_text_group_entry,
)

# 1. Definíció regisztrációja a skinben (pl. Voice Target gombok)
items = [
    {"label": "OSC A", "handle": "ActiveA", "rect": (0, 20, 166, 58)},
    {"label": "OSC B", "handle": "ActiveB", "rect": (170, 20, 166, 58)},
]
defn = build_titled_active_text_group_definition(
    "voiceNativeTargets",
    "PAN / DETUNE",
    items,
    size=(336, 80),
    skins_dir=skins_folder,
)

# 2. Lap szintű példányosítás és paraméter-összerendelés
entry = titled_active_text_group_entry(
    "Voice Target Scope",
    "voiceNativeTargets",
    (930, 388, 336, 80),
    mappings=[
        ("ActiveA", "Parameter 2676"),
        ("ActiveB", "Parameter 2677"),
    ],
    gates=[root_gate],
    focus="Yes",
)
```
