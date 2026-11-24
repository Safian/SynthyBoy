# Vital Import Core Changes C1-C3 Report

## 1. Pontonként kész / Mi maradt

- **1. C1: Custom Vital wavetable selectable (KÉSZ)**
  - `oscN.wavetable` a `kWavetablePresetTable` (index 13, "Preset table") értéket kapja, ha `caps.customWavetables` aktív.
  - A renderelt 16 bites PCM frame-ek bekerülnek a `res.wavetables` gyűjteménybe (`ImportedWavetable`).
  - Létrejött a `ContentStore` interfész, a `ContentRefs` / `ContentWavetableRef` referencia struktúra, valamint a `UserWavetableStore` implementáció a `user.imported-vital` ContentPack kezeléséhez és atomi manifest-íráshoz (`contentpack.json`).
  - SHA-256 alapú deduplikáció implementálva: ismételt azonos tábla importálásakor a meglévő fájl útvonalát adja vissza újraírás nélkül.
  - Ha `caps.customWavetables` hamis, megtartja a legközelebbi beépített táblát és az `approximated / nearestWavetable` bejegyzést.

- **2. C2: Custom LFO shape selectable (KÉSZ)**
  - `lfoN.shape` a `kLfoShapeCustom` (index 12, "Custom") értéket kapja, ha `caps.customLfoShapes` aktív.
  - Az LFO vonal pontjai (`pl.line`) átkerülnek a `res.lfoShapes` listába (`ImportedLfoShape`, x, y, power).
  - Ha a forma pontosan egyezik beépített formával (`matchBuiltinLfoShape >= 0`), shortcutként a beépített forma marad kiválasztva.
  - Custom választás esetén nem keletkezik `nearestLfoShape` approximated riporttétel.
  - Ha `caps.customLfoShapes` hamis, megtartja a legközelebbi beépített formát és a riporteemet.

- **3. C3: .sbpreset 'content' és 'import' szekciók, serializeImportedPreset, author szabály (KÉSZ)**
  - Új `savePreset (meta, values, content, allowEmptyAuthor)` túlterhelés a `core/src/preset.cpp`-ben és `core/include/synthyboy/preset.h`-ban.
  - Meglévő hívók változatlanul `allowEmptyAuthor = false` defaulttal futnak (üres author esetén "Safian Szabolcs").
  - Importált presetet `allowEmptyAuthor = true`-val mentve az üres author üres marad (`""`).
  - `serializeImportedPreset (const ImportResult& result, const PresetMeta& finalMeta, const ContentRefs& contentRefs)` implementálva a `core/vital-import/vital_import.cpp`-ben.
  - A `content` szekció a `state` objektumon belül kerül mentésre (`content.wavetables` és `content.lfo_shapes` adatokkal).
  - Additív `"import"` szekció formázása:
    - `"source"`: `format` ("vital"), `version`, `sha256`, `preset_name`, `comments`, `macro_names` [4].
    - `"approximated"`: legfeljebb 64 egyedi, nem üres saját paraméterkulcs az `approximated` riporttételekből.
  - 1 MiB méretkorlát ellenőrzése.

- **4. C4 / Tisztítás és Round-trip teszt (KÉSZ)**
  - Minden `TODO(C1)`, `TODO(C2)`, `TODO(C3)` marker eltávolítva a kódból és a tesztekből.
  - `vi_fixture_custom_wavetable` frissítve: ellenőrzi a 13-as ("Preset table") indexet és a riport approximation hiányát.
  - `vi_fixture_custom_lfo` frissítve: ellenőrzi a 12-es ("Custom") indexet mind a nem használt (LFO 3), mind a használt (LFO 4) custom formákra, és a riport approximation hiányát.
  - Új `vi_import_serialize_load_round_trip` teszt:
    - Vital preset importálása custom wavetable-lel és custom LFO formával, üres authorral.
    - `UserWavetableStore` mentés és deduplikáció ellenőrzése.
    - `serializeImportedPreset` futtatása.
    - `readPresetMeta` ellenőrzése (üres author megmarad).
    - `loadPreset` teljes betöltés: paraméterértékek megegyeznek, `content.wavetables` (pack, file, sha256, frames, osc) és `content.lfo_shapes` (pontok x, y, power) pontosan visszatöltődnek.
  - Új `user_wavetable_store` és `preset_empty_author_rule` tesztek.

- **Mi maradt**: semmi (minden pont 100%-ban kész, a tesztek zöldek).

---

## 2. Érintett fájlok

Csak a megadott core fájlok módosultak. A `core/engine-vital` és a shell kódok (`shell-clap`, `shell-juce`) **NEM** lettek módosítva. Új paraméterkulcs nem lett bevezetve (1484 kulcs változatlan).

1. `core/include/synthyboy/content_pack.h` — `serializeContentPackManifest` és `writeContentPackManifest` deklarációk.
2. `core/include/synthyboy/params.h` — `kWavetablePresetTable = 13` konstans.
3. `core/include/synthyboy/preset.h` — `savePreset` túlterhelés `allowEmptyAuthor` opcióval.
4. `core/include/synthyboy/vital_import.h` — `ContentWavetableRef`, `ContentRefs`, `ContentStore`, `UserWavetableStore`, `PresetStore`, `BatchOptions`, `serializeImportedPreset` deklarációk.
5. `core/src/content_pack.cpp` — Manifest JSON szerializáció és atomi írás implementáció.
6. `core/src/preset.cpp` — `savePreset` `allowEmptyAuthor` logika.
7. `core/src/vital_content.cpp` — `UserWavetableStore` implementáció (deduplikáció, 16 bites `.sbwt` frame-fájlok, manifest frissítés).
8. `core/vital-import/vital_import.cpp` — `kWavetablePresetTable` és `kLfoShapeCustom` kiválasztás, `serializeImportedPreset` implementáció.
9. `core/tests/test_preset.cpp` — `preset_empty_author_rule` egységteszt.
10. `core/vital-import/tests/test_vital_import.cpp` — Fixture tesztek frissítése, `user_wavetable_store` és `vi_import_serialize_load_round_trip` tesztek.

---

## 3. A kapuk kimenete szó szerint

Parancs:
```bash
cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=OFF && cmake --build build -j8 && ctest --test-dir build
```

Kimenet:
```
-- CLAP version: 1.2.7
-- SYNTHYBOY_WITH_VITAL=ON: Vital-derived engine is built (core/engine-vital)
-- Configuring done (0.3s)
-- Generating done (0.1s)
-- Build files have been written to: /Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-importc/build
[  1%] Built target synthyboy_filters_serum
[  2%] Built target synthyboy_sampleosc
[  4%] Built target synthyboy_shapes
[  6%] Built target synthyboy_fx_serum_b
[  8%] Built target synthyboy_fx_serum_a
[ 12%] Built target synthyboy_wtedit
[ 15%] Built target synthyboy_test_shapes
[ 16%] Built target synthyboy_test_fx_serum_a
[ 47%] Built target synthyboy_engine_vital
[ 57%] Built target synthyboy_core
[ 58%] Building CXX object core/tests/CMakeFiles/synthyboy_test_params.dir/test_params.cpp.o
[ 58%] Building CXX object core/tests/CMakeFiles/synthyboy_test_state.dir/test_state.cpp.o
[ 59%] Building CXX object core/tests/CMakeFiles/synthyboy_test_caps.dir/test_engine_caps.cpp.o
[ 59%] Linking CXX executable synthyboy_test_formula
[ 59%] Linking CXX executable synthyboy_test_wtedit
[ 59%] Linking CXX executable synthyboy_test_filters_serum
[ 59%] Linking CXX executable synthyboy_test_tuning
[ 59%] Building CXX object core/tests/CMakeFiles/synthyboy_test_keys.dir/test_new_keys.cpp.o
ld: warning: ignoring duplicate libraries: '../libsynthyboy_filters_serum.a'
ld: warning: ignoring duplicate libraries: 'libsynthyboy_wtedit.a'
[ 61%] Built target synthyboy_test_formula
[ 61%] Built target synthyboy_test_tuning
[ 62%] Built target synthyboy_test_filters_serum
[ 62%] Built target synthyboy_test_wtedit
[ 62%] Building CXX object shell-clap/CMakeFiles/synthyboy_static.dir/synthyboy_clap.cpp.o
[ 62%] Building CXX object core/tests/CMakeFiles/synthyboy_test_events.dir/test_events.cpp.o
[ 62%] Building CXX object core/tests/CMakeFiles/synthyboy_test_state_v3.dir/test_state_v3.cpp.o
[ 63%] Built target synthyboy_test_preset
[ 63%] Building CXX object core/tests/CMakeFiles/synthyboy_test_engine.dir/test_engine.cpp.o
[ 64%] Linking CXX executable synthyboy_test_caps
[ 64%] Built target synthyboy_test_caps
[ 65%] Building CXX object core/tests/CMakeFiles/synthyboy_test_rt.dir/test_rt_alloc.cpp.o
[ 66%] Linking CXX executable synthyboy_test_state
[ 67%] Linking CXX static library libsynthyboy_static.a
[ 67%] Linking CXX executable synthyboy_test_params
[ 67%] Built target synthyboy_test_state
[ 67%] Built target synthyboy_static
[ 67%] Built target synthyboy_test_params
[ 67%] Building CXX object core/tests/CMakeFiles/synthyboy_test_threads.dir/test_threads.cpp.o
[ 67%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_engine.dir/test_vital_engine.cpp.o
[ 67%] Linking CXX executable synthyboy_test_keys
[ 68%] Linking CXX executable synthyboy_test_events
[ 70%] Built target synthyboy_test_keys
[ 70%] Built target synthyboy_test_events
[ 70%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_params.dir/test_vital_params.cpp.o
[ 96%] Built target synthyboy_test_fx_serum_b
[ 96%] Building CXX object tools/paramdump/CMakeFiles/synthyboy_paramdump.dir/paramdump.cpp.o
[ 96%] Built target synthyboy_clap
[ 97%] Built target synthyboy_test_vital_audio
[ 97%] Building CXX object core/tests/CMakeFiles/synthyboy_test_shell_clap.dir/test_shell_clap.cpp.o
[ 97%] Linking CXX executable synthyboy_test_vital_wp123
[ 97%] Linking CXX executable synthyboy_test_vital_import_crosscheck
[ 97%] Built target synthyboy_test_vital_wp123
[ 97%] Built target synthyboy_test_vital_import_crosscheck
[ 97%] Linking CXX executable synthyboy_test_sampleosc
[ 98%] Linking CXX executable synthyboy_test_wavetable_render
[ 98%] Built target synthyboy_test_sampleosc
[ 98%] Linking CXX executable synthyboy_bench
[ 98%] Built target synthyboy_test_wavetable_render
[ 98%] Built target synthyboy_bench
[ 99%] Linking CXX executable synthyboy_paramdump
[ 99%] Built target synthyboy_paramdump
[100%] Linking CXX executable synthyboy_test_shell_clap
ld: warning: ignoring duplicate libraries: '../libsynthyboy_core.a'
[100%] Built target synthyboy_test_shell_clap
Test project /Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-importc/build
      Start  1: synthyboy_test_tuning
 1/29 Test  #1: synthyboy_test_tuning ....................   Passed    0.17 sec
      Start  2: synthyboy_test_formula
 2/29 Test  #2: synthyboy_test_formula ...................   Passed    0.18 sec
      Start  3: synthyboy_test_filters_serum
 3/29 Test  #3: synthyboy_test_filters_serum .............   Passed    0.21 sec
      Start  4: synthyboy_test_wtedit
 4/29 Test  #4: synthyboy_test_wtedit ....................   Passed    0.69 sec
      Start  5: synthyboy_test_shapes
 5/29 Test  #5: synthyboy_test_shapes ....................   Passed    0.03 sec
      Start  6: synthyboy_test_params
 6/29 Test  #6: synthyboy_test_params ....................   Passed    0.66 sec
      Start  7: synthyboy_test_state
 7/29 Test  #7: synthyboy_test_state .....................   Passed    6.91 sec
      Start  8: synthyboy_test_keys
 8/29 Test  #8: synthyboy_test_keys ......................   Passed    0.45 sec
      Start  9: synthyboy_test_caps
 9/29 Test  #9: synthyboy_test_caps ......................   Passed    0.31 sec
      Start 10: synthyboy_test_state_v3
10/29 Test #10: synthyboy_test_state_v3 ..................   Passed    5.91 sec
      Start 11: synthyboy_test_events
11/29 Test #11: synthyboy_test_events ....................   Passed    0.93 sec
      Start 12: synthyboy_test_preset
12/29 Test #12: synthyboy_test_preset ....................   Passed    0.02 sec
      Start 13: synthyboy_test_shell_clap
13/29 Test #13: synthyboy_test_shell_clap ................   Passed    0.40 sec
      Start 14: synthyboy_test_engine
14/29 Test #14: synthyboy_test_engine ....................   Passed    3.91 sec
      Start 15: synthyboy_test_rt
15/29 Test #15: synthyboy_test_rt ........................   Passed    5.41 sec
      Start 16: synthyboy_test_threads
16/29 Test #16: synthyboy_test_threads ...................   Passed   15.47 sec
      Start 17: synthyboy_test_vital_engine
17/29 Test #17: synthyboy_test_vital_engine ..............   Passed    1.98 sec
      Start 18: synthyboy_test_vital_params
18/29 Test #18: synthyboy_test_vital_params ..............   Passed    1.63 sec
      Start 19: synthyboy_test_vital_audio
19/29 Test #19: synthyboy_test_vital_audio ...............   Passed   30.83 sec
      Start 20: synthyboy_test_vital_wp123
20/29 Test #20: synthyboy_test_vital_wp123 ...............   Passed    5.96 sec
      Start 21: synthyboy_test_vital_dsp
21/29 Test #21: synthyboy_test_vital_dsp .................   Passed    3.34 sec
      Start 22: synthyboy_test_wavetable_render
22/29 Test #22: synthyboy_test_wavetable_render ..........   Passed    1.67 sec
      Start 23: synthyboy_test_vital_import
23/29 Test #23: synthyboy_test_vital_import ..............   Passed    0.98 sec
      Start 24: synthyboy_test_vital_import_crosscheck
24/29 Test #24: synthyboy_test_vital_import_crosscheck ...   Passed    0.22 sec
      Start 25: synthyboy_test_fx_serum_a
25/29 Test #25: synthyboy_test_fx_serum_a ................   Passed    0.01 sec
      Start 26: synthyboy_test_fx_serum_b
26/29 Test #26: synthyboy_test_fx_serum_b ................   Passed    0.21 sec
      Start 27: synthyboy_test_sampleosc
27/29 Test #27: synthyboy_test_sampleosc .................   Passed    0.18 sec
      Start 28: synthyboy_paramdump
28/29 Test #28: synthyboy_paramdump ......................   Passed    0.21 sec
      Start 29: synthyboy_paramdump_kick_pages
29/29 Test #29: synthyboy_paramdump_kick_pages ...........   Passed    0.01 sec

100% tests passed out of 29

Label Time Summary:
slow    =  30.83 sec*proc (1 test)

Total Test time (real) =  88.89 sec
```

---

## 4. Élőben ellenőrizendők

1. **Host-oldali fájlútvonalak és felhasználói könyvtárak**: A Kick / desktop shell környezetben a `UserWavetableStore`-nak átadott gyökérkönyvtár jogosultságai (írási engedély az alkalmazás adatkönyvtárában macOS / Windows / Linux alatt).
2. **Preset mentés és betöltés UI interakció**: Ellenőrizendő, hogy a mentett `.sbpreset` fájlok megjelennek-e a Kick vagy plugin preset böngészőjében, és a `user.imported-vital` csomag wavetable-jei hiba nélkül megszólalnak-e a lejátszás során (`missing_content` jelző tiszta marad).
3. **Valódi Vital preset fájlok (`tests/vital-presets`)**: Amennyiben a felhasználó saját preset kollekciója elérhetővé válik, a `ctest -R vital_import` futtatása a valós fájlokkal az esetlegesen felmerülő ritka Vital JSON mezők validálására.
