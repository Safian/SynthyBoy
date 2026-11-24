# Spektrumelrendezések Áttervezése: Téglalap Alakú Grafikonok és Oldalszintű Környezet

> **Tulajdonosi döntés és alapelv (2026-11-24):**  
> A 142 px-es kompakt spektrum alsó csíkban túl lapos; ahol a spektrum ilyen csíkba szorul, a teljes környezetét át kell tervezni, hogy használható téglalap alakú grafikon legyen. A 213 px-es teljes szélességű minta sem automatikus cél (mivel 1280 px szélesség mellett még mindig lapos, 6:1 arányú szalag maradna, és elnyomná a felette lévő vezérlőket).  
> A panel szélességét a környezet átrendezésével szűkítjük, a vezérlőket mellé helyezzük, miközben minden érintési felületet (legalább 56 px), olvasható Hz/dB skálákat, valamint az aktív és csendes állapotot hiánytalanul megőrizzük.

---

## 1. Érintett oldalak és forráskódbeli spektrumelrendezések

A forráskód részletes átvizsgálása alapján a spektrum ábrázolása az alábbi helyeken fordul elő és hasznosul újra:

| Komponens / Hely | Forrásfájl | Jelenlegi elrendezés | Állapot / Probléma |
|---|---|---|---|
| **MacroView (`SpectrumStrip`)** | [shell-mpc/ui/mod_macros.h](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/shell-mpc/ui/mod_macros.h#L22-L86), [shell-mpc/overlay/macro_ui.h](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/shell-mpc/overlay/macro_ui.h) | `(8, 480, 1264, 140)` alsó csík | **Elsődlegesen érintett oldal.** 8 makrókártya alatt szorul a 140 px-es alsó sávba. A belső rajzterület mindössze `1244 × 98 px` (**12.69 : 1** képarány), ahol a frekvenciatüskék és a dinamika lapos sávvá torzulnak. |
| **MasterScreen (`PeakMeterDisplay`)** | [shell-mpc/ui/voice_master.h](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/shell-mpc/ui/voice_master.h#L425-L535), [shell-mpc/overlay/master_ui.h](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/shell-mpc/overlay/master_ui.h) | Nincs spektrum, csak 40 px kivezérlésjelző | **Újrahasznosítási terület.** A Master kimeneti oldalon nincs frekvenciatartományi felügyelet. A téglalap alakú spektrum ideális a master összegző sáv (Output) mellé helyezve. |
| **SpectrumView (Overlay nézet)** | [shell-mpc/overlay/views.h](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/shell-mpc/overlay/views.h#L609-L729), [shell-mpc/overlay/overlay_layout.json](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/shell-mpc/overlay/overlay_layout.json#L37-L44) | `meterRow` natív 32 méter helye | Az audio thread `SpectrumRing` gyűrűjét olvassa, 256 pont, logaritmikus 20 Hz .. 20 kHz skála, -60 .. -12 dBFS skála. |
| **Oscillator Spectrum (`display::draw`)** | [shell-mpc/ui/display.h](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/shell-mpc/ui/display.h#L20-L48), [shell-mpc/overlay/osc_display.h](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/shell-mpc/overlay/osc_display.h) | `396 × 190 px` (Play harmad), `680 × 360 px` (Teljes) | Már eleve arányos téglalap alakú rajzterület a Spectral / Harmonic oszcillátor megjelenítéséhez. |
| **Filter / EQ Response (`FilterDisplay`, `FxDisplay`)** | [shell-mpc/ui/filter_display.h](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/shell-mpc/ui/filter_display.h), [shell-mpc/ui/fx_display.h](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/shell-mpc/ui/fx_display.h) | `680 × 310 px` (Filter), `520 × 260 px` (EQ) | Hasonló logaritmikus Hz és dB koordinátarendszerű téglalap alakú felületek (2.0 : 1 .. 2.2 : 1). |

---

## 2. Miért volt hibás a 142 px és a 213 px teljes szélességű minta?

1. **A 142 px-es alsó sáv (Before)**:
   - Külső méret: `1264 × 140 px` (arany: **9.03 : 1**).
   - Belső aktív rajzterület: `1244 × 98 px` (arány: **12.69 : 1**).
   - Felbontás: 48 dB tartományban mindössze **~1.6 pixel / dB**.
   - Probléma: A görbe extrém módon összenyomódik függőlegesen; a szűrőmeredekségek (12/24 dB/oktáv), a formánsok és a harmonikusok csúcsai alig kivehető mikro-rezgésekké laposodnak.
2. **A 213 px-es teljes szélességű kísérlet**:
   - Külső méret: `1294 × 213 px` (arány: **6.07 : 1**).
   - Probléma: Bár magasabb, a teljes 1280-as szélesség mentén még mindig egy elnyújtott panorámacsík marad. Ha a MacroView-ba helyezzük teljes szélességben, a felette lévő 8 makrókártyától 73 px-t vesz el, amitől a forgatógombok és a cél-chipek (modulációs címkék) használhatatlanul kicsire zsugorodnának.

**A helyes tervezési elv:** A spektrumot nem a teljes szélességben nyújtjuk, hanem **szűkítjük a szélességét (pl. 628 px-re)**, így a magassága 260–548 px-re növelhető anélkül, hogy a vezérlők helyét feláldoznánk.

---

## 3. Konkrét Előtte / Utána Tervek Teljes Oldali Kontextusban (1280 × 628 px)

### Elrendezéstípus 1: Macros oldal (MacroView)

#### Jelenlegi állapot (Before)
- **Fejléc:** `(0, 0, 1280, 64)` MACROS cím, Bank 1 / Bank 2 váltó.
- **Makrók:** 2 sor × 4 oszlop, 8 kártya `(8, 72, 1264, 400)` területen (kártyánként `310 × 196 px`).
- **Spektrum:** `(8, 480, 1264, 140)` alsó csíkban, belső rajzterület `1244 × 98 px` (**12.69 : 1**).
- **Előnézeti képek:**
  - Aktív állapot: [macros-layout-before-active.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/screens/macros.jpg)
  - Csendes állapot: [macros-layout-before-silent.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/screens/macros.jpg)

---

#### Terv A: Aszimmetrikus Kétszintes Elrendezés (Bank 1 + Téglalap Spektrum fent, Bank 2 lent)
- **Felső szint (`y = 72..384`, magasság 312 px):**
  - **Bal oldal (`8, 72, 628, 312`):** Bank 1 (Macro 1, 2, 3, 4) 2 sor × 2 oszlopos rácsban.
    - Kártyaméret: `308 × 150 px`.
    - Knob átmérő: 88 px, címke és érték a tárcsa mellett.
    - Érintési felület: cél-chip magasság 46 px, kombinált kártya érintés > 150 px (szabály: >= 56 px teljesül).
    - Q-Link: 1 sárga fókuszkeret a Bank 1 körül, ha aktív.
  - **Jobb oldal (`644, 72, 628, 312`):** **Téglalap Alakú Studio Spektrum Analizátor Panel**.
    - Külső befoglaló méret: **`628 × 312 px`** (Képarány: **2.01 : 1** ~ 18:9 / 2:1).
    - Belső rajzterület: **`604 × 262 px`** (Képarány: **2.31 : 1**).
    - Rajzterület magassága: **262 px** (az eredeti 98 px-hez képest **+167% függőleges növekedés, 5.46 px / dB**).
- **Alsó szint (`y = 392..620`, magasság 228 px):**
  - Bank 2 (Macro 5, 6, 7, 8) teljes szélességű vízszintes sorban `(8, 392, 1264, 228)`.
  - Kártyaméret: `310 × 228 px` (kényelmes, tágas felület, bőséges hely a modulációs céloknak).
  - Q-Link: Bank 2 sárga keret, ha a Bank 2 aktív.
- **Előnézeti képek:**
  - Aktív állapot: [macros-layout-after-opt1-active.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/screens/macros.jpg)
  - Csendes állapot: [macros-layout-after-opt1-silent.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/screens/macros.jpg)

---

#### Terv B: Szimmetrikus Központi Pro Studio Analizátor (Középen 4:3 Spektrum, kétoldalt szegélyezve)
- **Bal oszlop (`8, 72, 310, 548`):** Bank 1 (Macro 1..4) függőleges oszlopban (4 kártya egymás alatt).
  - Kártyaméret: `310 × 131 px` (tárcsa balra, modulációs cél-chipek jobbra).
  - Q-Link: 1 sárga keret a bal oszlop körül (1..4 gombok).
- **Középső panel (`326, 72, 628, 548`):** **Klasszikus 4:3 Pro Studio Spektrum Analizátor**.
  - Külső befoglaló méret: **`628 × 548 px`** (Képarány: **1.15 : 1** ~ 4:3 aranymetszés).
  - Belső rajzterület: **`604 × 498 px`** (Képarány: **1.21 : 1**).
  - Rajzterület magassága: **498 px** (az eredeti 98 px-hez képest **+408% függőleges növekedés, 10.38 px / dB**).
  - Kivételes mastering- és hangtervezési precizitás: minden részhang, rezonanciatüske és formáns egyértelműen leolvasható.
- **Jobb oszlop (`962, 72, 310, 548`):** Bank 2 (Macro 5..8) függőleges oszlopban.
  - Kártyaméret: `310 × 131 px`.
- **Előnézeti képek:**
  - Aktív állapot: [macros-layout-after-opt2-active.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/screens/macros.jpg)
  - Csendes állapot: [macros-layout-after-opt2-silent.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/screens/macros.jpg)

---

### Összehasonlító tábla (Macros oldal)
Egyetlen áttekintő, nagyfelbontású összehasonlító táblán a Before, a Terv A és a Terv B:
👉 [macros-spectrum-redesign-comparison.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/screens/macros.jpg)

---

### Elrendezéstípus 2: Master kimenet (MasterScreen oldal)

#### Jelenlegi állapot (Before)
- A Master Main oldalon a bal felső OUTPUT blokkban `(8, 72, 640, 228)` csak Volume, Spread, Stereo tárcsák és egy 40 px széles `PeakMeterDisplay` kivezérlésjelző található.
- Frekvenciatartományi felügyelet hiányzik a kimeneti sávban.
- Előnézet: [master-layout-before-active.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/screens/master.jpg)

#### Áttervezett Master elrendezés (After)
- **Felső sor (`y = 72..332`, magasság 260 px):**
  - **Bal oldal (`8, 72, 628, 260`):** MASTER OUTPUT blokk (Volume, Spread, Stereo tárcsák, Limiter, Quality LED kapcsolók és sztereó kivezérlésjelző).
  - **Jobb oldal (`644, 72, 628, 260`):** **Beépített Téglalap Alakú Master Spektrum Analizátor**.
    - Külső panelméret: **`628 × 260 px`** (Képarány: **2.42 : 1**).
    - Belső rajzterület: **`604 × 210 px`** (Képarány: **2.88 : 1**).
    - Rajzterület magassága: **210 px**.
- **Alsó sor (`y = 340..620`, magasság 280 px):**
  - Bal oldal `(8, 340, 780, 280)`: Egységesített PITCH & VOICE blokk (Tune, Transpose, Polyphony, Steal, Voice meter).
  - Jobb oldal `(796, 340, 476, 280)`: GLIDE blokk (Portamento idő, slope és görbekijelző).
- **Előnézeti képek:**
  - Aktív állapot: [master-layout-after-active.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/screens/master.jpg)
  - Csendes állapot: [master-layout-after-silent.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/screens/master.jpg)
- **Master összehasonlító tábla:**
  👉 [master-spectrum-redesign-comparison.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/screens/master.jpg)

---

## 4. Önálló Téglalap Alakú Spektrum Panelek Részletes Specifikációja

| Panel Változat | Használati Hely | Külső Méret | Külső Képarány | Belső Rajzterület | Belső Képarány | Magasságnövekedés a 140px csíkhoz képest | Előnézetek (Aktív / Csendes) |
|---|---|---|---|---|---|---|---|
| **628 × 312 px (2.0 : 1)** | Macros Terv A (jobb felső) | `628 × 312 px` | **2.01 : 1** | `604 × 262 px` | **2.31 : 1** | **+167%** (262 px vs 98 px) | [spectrum-rect-628x312-active.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/ui-components/komponensek/spectrum-rect-628x312-active.png)<br>[spectrum-rect-628x312-silent.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/ui-components/komponensek/spectrum-rect-628x312-silent.png) |
| **628 × 548 px (4 : 3)** | Macros Terv B (középső pro) | `628 × 548 px` | **1.15 : 1** | `604 × 498 px` | **1.21 : 1** | **+408%** (498 px vs 98 px) | [spectrum-rect-628x548-active.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/ui-components/komponensek/spectrum-rect-628x548-active.png)<br>[spectrum-rect-628x548-silent.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/ui-components/komponensek/spectrum-rect-628x548-silent.png) |
| **628 × 260 px (2.4 : 1)** | Master Output integráció | `628 × 260 px` | **2.42 : 1** | `604 × 210 px` | **2.88 : 1** | **+114%** (210 px vs 98 px) | [spectrum-rect-628x260-active.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/ui-components/komponensek/spectrum-rect-628x260-active.png)<br>[spectrum-rect-628x260-silent.png](file:///Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-spectrum-teglalap/docs/ui-components/komponensek/spectrum-rect-628x260-silent.png) |

---

## 5. Skála- és Grafikai Szabályok Megőrzése

1. **Logaritmikus frekvenciaskála (20 Hz .. 20 kHz):**
   - 3 évtizedes pontos skálázás (`log10(f / 20) / 3.0`).
   - Fővonalak és feliratok: 100 Hz, 1 kHz, 10 kHz `#27306A` vonalakkal és ticks jelölésekkel.
   - Segédvonalak: 30, 50, 70, 200, 300, 500, 700, 2k, 3k, 5k, 7k Hz finom `#1A2250` ráccsal.
2. **Lineáris dB skála (-60 dBFS .. -12 dBFS / 0 dBFS):**
   - Fő szintek: -48, -36, -24, -12 dBFS, és az új magasabb paneleknél 6 dB-es segédvonalak (-54, -42, -30, -18, -6 dBFS).
   - A dB címkék sötétített háttérmezőt kaptak (`(14, 19, 42)`), így a hálóvonalak vagy a görbe nem zavarja az olvashatóságot.
3. **Kettős állapot kezelése:**
   - **Aktív állapot:** Élénk `#2EE6F0` cián görbe (`flow[0]`, 2.0 px vastagság antialiasinggal), alatta áttetsző vertikális gradiens kitöltés (alpha 58-ról 12-re csökkenve a padló felé).
   - **Csendes állapot (Quiet state):** A rács és a feliratok hiánytalanul láthatók maradnak; a -60 dBFS padlóvonalon finom, diszkrét cián alapvonal (`#2EE6F0`, alpha ~85) jelzi a modul készenlétét.
4. **Hardveres érintési felületek (MPC szabvány):**
   - Minden vezérlő (forgatógomb, felirat, modulációs chip, gomb) érintési mérete meghaladja a kötelező **56 px** minimumot (`AGENTS.md` Rule 3).

---

## 6. Katalógus- és Ügynökfegyelem

1. **A képes katalógus épsége:**  
   A felhatalmazás alapján a végleges katalógusba (`docs/ui-components/kepes-katalogus.html`) csak a tulajdonos által kiválasztott terv kerülhet be, amelyet az AGY3 profilon futó `ui-komponenstar` worker frissít. A most elkészített tervek jóváhagyásra várnak; a katalógus HTML fájl érintetlen maradt.
2. **Párhuzamos workerek védelme:**  
   A futó Macro/Play/OSC workerek C++ forrásai (`shell-mpc/ui/mod_macros.h`, `shell-mpc/overlay/macro_ui.h`, `screen.h`, stb.) nem módosultak. A feladat az oldalszintű áttervezés és bemutatható tervek elkészítése volt önálló worktree-ben.

---

## 7. Döntési javaslat a Tulajdonosnak

- **Terv A (Aszimmetrikus 2.0 : 1 Analizátor):**  
  *Ajánlott*, ha fontos, hogy a 8 makrókártya mindegyike hagyományos nagy méretű forgatótárcsával és alsó címkesorral rendelkezzen, a spektrum pedig egy 628 × 312 px-es stúdióablakban fusson a jobb felső sarokban.
- **Terv B (Központi 4:3 Pro Analizátor):**  
  *Ajánlott*, ha a hangsúly a spektrumanalízisen van (pl. sound design, live monitoring), ahol a 498 px rajzmagasság maximális felbontást nyújt a zenei részhangok elemzéséhez.
