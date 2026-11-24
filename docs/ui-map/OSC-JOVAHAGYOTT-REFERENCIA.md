# OSC Jóváhagyott Terv Referencia és Tulajdonosi Döntési Bizonyíték

Készült: 2026-11-24  
Vonatkozó feladat: `agy/osc-jovahagyott-terv-azonositas`  
Hivatkozási szabály: `AGENTS.md` (Product scope 2026-11-23, Release packaging 2026-11-20), `docs/ui-components/README.md`

---

## 1. Tulajdonosi korrekció és elavult tervek elhatárolása

1. **`before/Init_osca.png` történeti jellegű**:
   - A korábbi, már törölt OSC A referencia és `docs/ui-map/q-link-mapping/before/Init_osca.png` a develop `6e89be80` állapot előtti, 27 vezérlős korábbi architektúrát tükrözi (felső sávban LEVEL/PAN/ROUTE, alatta típus-sáv, jobb oldali TUNE oszlop, 238 px magas wavetable görbe).
   - Ez a kép történeti előzmény (before), **tilos jóváhagyott vagy érvényes alapként kezelni**.

2. **Új Gemini-tervek önmagukban nem elfogadottak**:
   - Az `agy/osc-curve-side-controls` ágon született összehasonlítás (`docs/ui-map/osc-curve-side-controls/`, `3dbfa138`: 672 × 578 px görbe + jobb oldali vezérlőrács) és a korábbi Q1–Q4 chipes fejléc-kísérletek (`7aa4faa1`) **nincsenek jóváhagyva**.
   - Az `osc-komponensek-elso-lepes` feladatban az owner rögzítette: *"Előző OSC összkép elvetve. Ne tervezz oldalsó menüt, felső sort vagy teljes képernyőt, a megbeszélt felső sor későbbi lépés."*

3. **Orkesztrációs koordináció a futó workerekkel**:
   - A párhuzamosan futó `osc-gorbe-atvetel` és `osc-gorbe-sajat-komponensek` ágakat a koordinátor **NEM integrálhatja** a `develop` ágba, mivel alapjukat vesztették.
   - Az érvényes fejlesztési lépés kizárólag a jóváhagyott natív komponensek leltára (`osc-komponensek-elso-lepes`).

---

## 2. A legutóbb tulajdonos által jóváhagyott OSC referencia és bizonyítékai

A projektben jelenleg fellelhető legfrissebb, dokumentált tulajdonosi jóváhagyással rendelkező referenciák:

### A) Funkcionális és elrendezési referencia: Bold Cut (Step 7)
- **Képi útvonal**:
  - `docs/ui-map/osc-play-bold-cut/after/Init_osca.png`
  - Kapcsolódó állapotok: `after/Init_osca_warp_on.png`, `after/Init_osca_type_wave.png`
- **Döntési bizonyíték**:
  - `docs/ui-map/osc-play-bold-cut/README.md`:
    > *"Owner decision 2026-11-21 ("Bold cut"), step 7: the Osc pages (Osc A, B, C, Sub, Noise) and the Play page; spec docs/ui-map/SIMPLIFY.md (B, B2, B3, Play row), selector rules docs/ui-map/env-filter-lfo-plan.md."*
- **Főbb paraméterei**:
  - 27 kezelőszervről 12-re csökkentett alapnézet.
  - Felső sor: `[TYPE, SOURCE, WT POS, OCT]`. LEVEL és PAN a Mixerhez tartozik (csak-olvasható indikátor).
  - Típus-sáv és TUNE oszlop megszűnt: a finomhangolás kivételével a haladó paraméterek a MORE oldalra kerültek.
  - Alsó sáv: UNISON, WARP 1, WARP 2 és egyetlen MORE gomb.

### B) Hardveres és mérési referencia: MPC XL rögzítés (2026-11-21)
- **Képi útvonalak**:
  - `.hw-local/mpc-perf/prepared-osc-xl/wt-main-composed.png` és `wt-open-composed.png`
  - `.hw-local/screen-library/xl/overlay/osca.png` (commit `554b30b9`)
  - `docs/user-guide/img/xl-osc-a.png`
- **Döntési bizonyíték**:
  - `TODO.md` és `docs/dsp-knowledge/mpc-display.md`:
    > *"Az önálló Osc A sample/granular/spectral görbék a javított XL binárison A/B/A2 mellett mindkét irányban pixelpontosan egyeztek, végig ugyanazzal a GPU-helperrel (0 újraindulás); bizonyíték: .hw-local/mpc-perf/prepared-osc-xl/final-run.log."*
    > Commit: `c77b9b15` (2026-11-21 22:53:12).

### C) Natív skin és háttér-migrációs állapot (2026-11-23)
- **Dokumentáció**: `docs/ui-map/osc.md` (Frissítve: 2026-11-23, commit `0c0d8c5a`).
- **Háttérkép**: `scripts/mpc-skin-extras/overlay-backgrounds/overlay_bg_osc.png` (commit `3f4a0348`).
- **Aktuális katalógus-megjelenés**: `docs/ui-components/native-katalogus.html` (Osc szakasz: *Jelenlegi SynthyBoy-nézet · XL* és *Csak a natív réteg*).

---

## 3. Összegzés és nyitott tervezési állapot

1. Teljes, újratervezett **natív OSC képernyőterv jelenleg NEM jóváhagyott**.
2. A jóváhagyott natív komponensek köre (`docs/ui-components/kepes-katalogus.html`):
   - 202 × 48 px és 202 × 56 px választó
   - 90 × 90 px tárcsa (PAN, LEVEL, Macro)
   - 48 px magas gombok (POWER, EXPAND, akciógombok)
   - HZ/BPM/KEY szegmentált választó
   - **Kötelező érvényű owner szabály (commit `ec5506d6`):** Q-Link számok, jelvények (badge) vagy számsávok nem jelenhetnek meg a komponenseken vagy a terveken!
3. Az `osc-komponensek-elso-lepes` worker felelős a felhasználandó natív komponensek pontos leltáráért. Teljes képernyős elrendezést csak ezen komponensek véglegesítése és tulajdonosi jóváhagyása után szabad készíteni.
