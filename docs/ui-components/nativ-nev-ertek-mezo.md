# Közös natív név–érték mező (Native Name-Value Field)

Közös, újrahasználható natív komponens Akai MPC standalone érintőképernyőre és hardveres Q-Link / Data Wheel vezérlésre.

![A jelenleg használt Sub OCT mező, 40 px](../../components/native/name_value/img/catalog-name-value-40.png)

---

## 1. Tulajdonosi jóváhagyás és magassági változatok (2026-11-24)

A tulajdonosi jóváhagyás és specifikáció alapján:
> **"Az elfogadott 48 px magas név–érték mezőből készíts tényleges 40 és 32 px magas változatot a meglévő scripts/native_name_value.py közös komponensével és rendererével. Balra a név kereten kívül, jobbra csak a szám keretben, Q-Link-szám nélkül. Őrizd meg a 48 px-es alapváltozatot és a meglévő értékkötést, szerkesztést."**

### Magassági változatok paraméterei

| Változat | Teljes méret | Számkeret | Rádiusz | Név betűméret | Szám betűméret | Érintési felület | Vertikális nyereség |
|---|---|---|---|---|---|---|---|
| **48 px** (alap) | 125 × 48 px | 68 × 48 px | 8 px | 14.0 pt (SemiBold) | 16.0 pt (SemiBold) | 125 × 48 px | Bázis (referencia) |
| **40 px** (kompakt) | 125 × 40 px | 68 × 40 px | 6 px | 12.0 pt (SemiBold) | 14.0 pt (SemiBold) | 125 × 40 px | **-17% magasság** |
| **32 px** (minimál) | 125 × 32 px | 68 × 32 px | 5 px | 11.0 pt (SemiBold) | 12.0 pt (SemiBold) | 125 × 32 px | **-33% magasság** |

Ennek megfelelően a komponens vizuális felépítése mindhárom magasságnál azonos elvet követ:
- **Szélesség:** egységesen **125 px** (érintési felület szélessége >= 56 px az AGENTS.md követelmény szerint).
- **Bal oldalon (kereten kívül):** rövid paraméternév (pl. `SEM`, `CRS`, `OCT`, `FIN`).
  - Nincs körülötte doboz vagy keret; közvetlenül a hordozó MPC panel háttere látszik alatta.
  - MPC natív típus: `Label`, `type: "Name"`, adatforrás: `handleName: "Caption"`.
  - Szövegstílus: `Titillium Web SemiBold`, szín: `#8fa2c0` (`ff8fa2c0`), igazítás: `left verticallyCentred`.
- **Jobb oldalon (keretben):** az Akai MPC host által formázott aktuális számérték (pl. `0`, `+7`, `-12.50`).
  - Különálló, 68 px széles lekerekített téglalap keret (kitöltés: `#12182d`, szegély: `#1c2651`).
  - MPC natív típus: `Label`, `type: "Value"`, adatforrás: `handleName: "Data"`.
  - Szövegstílus: `Titillium Web SemiBold`, szín: `#ebeffe` (`ffebeffe`), igazítás: `horizontallyCentred verticallyCentred`.
- **Q-Link jelölés:** A sárga Q-Link szám és a helye **nem része a komponensnek**. A felületen 0 db sárga chip vagy szám található. A Q-Link hozzárendelés a host és a routing réteg szintjén érvényesül.

---

## 2. Meglévő NumberField és Label stílusok, érintőfelület és hardverkötés

A skin definícióban mindhárom méret ugyanazt a natív numerikus szerkesztést és paraméterkötést használja.
1. **Érintés és numerikus szerkesztés (teljes érintőfelület):**
   - A teljes 125 × {48, 40, 32} px cella aktív érintési felület (`ignoreMousePresses: False`). A 125 px-es vízszintes kiterjedés jóval meghaladja a minimális 56 px-es érintési követelményt.
   - `"Mouse Down"` és `"Enter Pressed"` eseményekre a host a standard numerikus overlay menüt hívja meg:
     - `handler: "Show Overlay"`
     - `handleName: "Data"`
     - `additionalData: "fx editor number menu"`
2. **Hardveres Q-Link és Data Wheel:**
   - `disableCoarseDataWheel: False` biztosítja, hogy a fizikai enkóder vagy az adatárcsa forgatásakor a paraméter közvetlenül léptethető legyen.
   - Paraméterkötés a `handle remapping` listán keresztül (`{"key": "Data", "value": "Parameter <id>"}`).
3. **Hardveres fókuszgyűrű:**
   - A beépített `Focus` réteg `whenVisible: "WhenFocussed"` logikával közvetlenül a jobb oldali számkeretet (68 × {48, 40, 32} px) emeli ki fizikai navigációkor.
4. **Alap háttér:**
   - A gyökérkomponens `backgroundData` háttere transzparens (`colour: "0"`), így a paraméternév keret nélkül fekszik a panelen.
   - A jobb oldali számkeret felületi háttere éles antialiasolt képi dekorációként (`Face`: `name_value_box_68x{h}.png`) vagy skin rajzolásként jelenik meg.

---

## 3. Bemutatott valódi komponensértékek és szövegilleszkedés

Mindhárom magasságban azonos tesztértékekkel igazoltuk a szövegilleszkedést és a margókat:
1. **`SEM [ +7 ]`**: Előjeles pozitív egész érték (kvint transzpozíció, a tulajdonos mintája).
2. **`CRS [ -12.50 ]`**: Két tizedesjegy pontosságú negatív előjeles lebegőpontos érték (durva hangolás).

### Szövegilleszkedés ellenőrzése:
- **48 px magasság:** Név 14 pt (28 px széles a 49 px mezőben), érték 16 pt ("-12.50" 47 px széles a 68 px dobozban). Függőleges margó: 18 px.
- **40 px magasság:** Név 12 pt (24 px széles a 49 px mezőben), érték 14 pt ("-12.50" 41 px széles a 68 px dobozban). Függőleges margó: 14.5 px.
- **32 px magasság:** Név 11 pt (21 px széles a 49 px mezőben), érték 12 pt ("-12.50" 36 px széles a 68 px dobozban). Függőleges margó: 11 px.

Mindegyik méretnél a leghosszabb előjeles tizedes érték esetén is kényelmes, legalább 13–16 px horizontális és 11–18 px vertikális margó marad.

---

## 4. Forrásfájlok és ellenőrzés

- **Komponens definíció:** [`scripts/native_name_value.py`](../../scripts/native_name_value.py)
  - `build_name_value_definition(...)`: Akai MPC skin JSON definíciót állít elő (balra név keret nélkül, jobbra érték keretben; automatikus 48/40/32 px preset).
  - `render_name_value_preview(...)`: Valós vizuális renderelés a komponensből a választott magassággal.
  - `create_name_value_entry(...)`: Fülre illeszthető paraméter-példányosító.
- **Komponensképek generátora:** [`scripts/render_native_catalog_cards.py`](../../scripts/render_native_catalog_cards.py)
  - Kimenet: [`docs/ui-components/../../components/native/name_value/img/catalog-name-value-40.png`](../../components/native/name_value/img/catalog-name-value-40.png)
- **Célzott tesztcsomag:** [`scripts/tests/test_native_name_value.py`](../../scripts/tests/test_native_name_value.py)
  - 11 egységteszt ellenőrzi a JSON struktúrát mindhárom méretben, a transzparens gyökérhátteret, a név/szám pozíciókat, a feliratkötéseket, az érintési (szélesség >= 56 px) és enkóder tulajdonságokat, a pixelszintű dobozkontrollt, valamint a sárga Q-Link chipek teljes hiányát.

---

## 5. Példányosítási minta különböző magasságokhoz

```python
import native_name_value as nnv

# 1. Előnézeti képek renderelése azonos szélességen (125 px):
comp_48 = nnv.render_name_value_preview("SEM", "+7", width=125, height=48)
comp_40 = nnv.render_name_value_preview("SEM", "+7", width=125, height=40)
comp_32 = nnv.render_name_value_preview("SEM", "+7", width=125, height=32)

# 2. MPC skin JSON definíció létrehozása adott magassággal:
skin_def_40 = nnv.build_name_value_definition(
    name="oscTuneSemi40",
    rect=(0, 0, 125, 40),
    skins_dir=skins_dir,
)

# 3. Fül elem példányosítása:
tab_entry_40 = nnv.create_name_value_entry("Osc Semi", "oscTuneSemi40", (100, 200, 125, 40), param=3320)
```

## 6. Egymás alatti (stacked) változat (2026-11-26)

`build_name_value_definition(..., stacked=True, caption_height=24, box_height=56, box_top=None, box_width=132)`: a név középre igazítva a mező FÖLÖTT áll (a mellette lévő tárcsa vagy címkézett választó feliratsorával egy vonalban), alatta a keretes érték `box_width` szélesen, középen, `font.value.large` betűvel. A doboz legalább 56 px magas (érintési cél); `box_top` a dobozt lejjebb teszi (a Master tárcsasorában a tárcsa középvonalára). Az alapértelmezett, oldalsó elrendezés és a meglévő képek nem változtak. Használja: Master minden számmezője (TRANSPOSE, BEND, POLY, VEL TRACK, PAD %, A4 HZ, MPE BEND), lásd [master-screen.md](master-screen.md).
