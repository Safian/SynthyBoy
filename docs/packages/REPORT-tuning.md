# Implementation Report: Microtonal Tuning (`core/tuning/`, Decision Q23)

## 1. Kész / Mi maradt (Summary of Completed Work)

Minden feladatpont 100%-ban, hiánytalanul elkészült (nincs félmegoldás):

1. **Parserek (Scala `.scl`, `.kbm` keyboard mapping, AnaMark `.tun` -> `Tuning` objektum):**
   - **KÉSZ.**
   - Hibakezelés: `ParseError` enum (`ok`, `emptyInput`, `tooLarge`, `invalidHeader`, `invalidScaleSize`, `invalidPitch`, `missingPitchEntries`, `invalidMapping`, `invalidTunSection`, `invalidTunEntry`, `ioError`) és `ParseResult` struktúra (sor- és hibaüzenet információval).
   - Robusztusság: Nincs kezeletlen kivétel vagy programösszeomlás; hiba esetén az eredeti `Tuning` objektum állapota érintetlen marad (`strong exception / error safety`).
   - Méret- és korlátvédelem: `kMaxTuningFileSize = 1 MB`, `kMaxScaleNotes = 1024`, `kMaxMappingSize = 1024`.
   - Formátum támogatás:
     - `.scl`: cent értékek (tizedespontos) és arányok (egész és `num/den` tört), tetszőleges skálaperiódus.
     - `.kbm`: mind a 7 fejlécparaméter (méret, első/utolsó MIDI, középbillentyű, referenciaszám, referenciafrekvencia, referenciasorozat-fok), billentyű-átképzés és `x` / `X` leképezetlen (némított) billentyűk.
     - `.tun`: AnaMark v1/v2 `[Tuning]`, `[Exact Tuning]`, `BaseFreq` beállítás, direkt 128 hang frekvenciatábla.
   - Kétirányú szerializáció / round-trip: `toScl()`, `toKbm()`, `toTun()`.

2. **`Tuning::frequencyForKey(int midiKey, double a4Hz)` és előszámolt 128 elemű tábla:**
   - **KÉSZ.**
   - Lock-free, valós idejű biztonság az audioszálon: $O(1)$ közvetlen tömbelérés (`std::array<double, 128>`), nulla allokáció, `noexcept`.
   - Alapértelmezés: 12-TET @ A4 = 440.0 Hz.
   - Koncertmagasság skálázás (`a4Hz` paraméter): dinamikus áthangolás tetszőleges referenciafrekvenciára.
   - Tartományellenőrzés: 0..127 tartományon kívüli vagy unmapped billentyűk esetén 0.0 Hz-et ad vissza, sosem olvas túl.

3. **Külső dinamikus hangolási forrás interfész (`ITuningSource`, MTS-ESP seam):**
   - **KÉSZ.**
   - Tiszta absztrakt C++ interfész (`core/include/synthyboy/tuning.h`):
     - `virtual bool isConnected() const noexcept = 0;`
     - `virtual double frequencyForKey (int midiKey) const noexcept = 0;`
     - `virtual bool query128Keys (std::array<double, 128>& outFrequencies) const noexcept = 0;`
     - `virtual const char* name() const noexcept;`
   - Semmilyen külső MTS-ESP könyvtárat nem linkel, tiszta illesztési felületet (seam) biztosít.

4. **Nyilvános fejléc és csomagstruktúra:**
   - **KÉSZ.**
   - Nyilvános fejléc: `core/include/synthyboy/tuning.h` a `synthyboy::tuning` névtérben.
   - Nincs motor (engine) behuzalozás (a specifikáció szerint).
   - Új csomag CMakeLists.txt: `core/tuning/CMakeLists.txt`.
   - `core/CMakeLists.txt`: pontosan 1 sor hozzáadva (`add_subdirectory(tuning)`).

5. **Tesztek (`core/tuning/test_tuning.cpp`):**
   - **KÉSZ.**
   - 12-TET matematikai ellenőrzése: $440 \cdot 2^{(k-69)/12}$ minden $k \in [0, 127]$ esetén $< 10^{-15} \ll 10^{-9}$ pontossággal.
   - Ismert `.scl` skálák: 19-EDO (oktávduplázás és arányok) és 12-fokú 5-limit Just Intonation (tiszta kvintek 3/2, nagytercek 5/4, oktávok 2/1).
   - `.kbm` tesztek: referenciahang átképzés (A4=432 Hz, C4=260 Hz) és fehér billentyűs mapping fekete billentyűk némításával (`x`).
   - `.tun` tesztek: AnaMark formátum olvasása és `[Exact Tuning]` felülbírálás ellenőrzése.
   - Malformed input tesztek: 36 különböző hibás és rosszindulatú bemenet (nullptr, üres, túlméretes, 0 nevező, negatív arány, nem-numerikus szemét, NaN/Inf, hiányos fejlécek és sorok). Mind elbukás nélkül, megfelelő hibakóddal lefutott, a `Tuning` belső állapota sértetlen maradt.
   - Round-trip tesztek: SCL (19-EDO, Just Intonation), KBM, TUN és 12-TET szerializáció és visszaolvasás $< 10^{-9}$ eltéréssel.
   - MTS-ESP seam tesztek: `MockDynamicTuningSource` kapcsolat és lekérdezés.
   - 8 teszteset, 1163 sikeres ellenőrzés.

**Mi maradt:**
- Semmi.

---

## 2. Érintett fájlok

1. `core/CMakeLists.txt` (módosítva: pontosan 1 új sor: `add_subdirectory(tuning)`)
2. `core/include/synthyboy/tuning.h` (új nyilvános fejléc)
3. `core/tuning/CMakeLists.txt` (új csomag build konfiguráció tesztekkel)
4. `core/tuning/tuning_internal.h` (új belső segédfüggvények szöveg- és számpolvasáshoz)
5. `core/tuning/tuning.cpp` (új implementáció: táblaszámítás, 12-TET, frekvencia lekérés)
6. `core/tuning/scl_parser.cpp` (új Scala `.scl` parser és `toScl()` szerializáló)
7. `core/tuning/kbm_parser.cpp` (új Scala `.kbm` parser és `toKbm()` szerializáló)
8. `core/tuning/tun_parser.cpp` (új AnaMark `.tun` parser és `toTun()` szerializáló)
9. `core/tuning/test_tuning.cpp` (új egységtesztek a runner stílusában)
10. `REPORT.md` (untracked riport)

---

## 3. Kapuk kimenete szó szerint

### Parancs:
`cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=OFF && cmake --build build -j8 && ctest --test-dir build`

### Kimenet:
```text
-- CLAP version: 1.2.7
-- SYNTHYBOY_WITH_VITAL=ON: Vital-derived engine is built (core/engine-vital)
-- Configuring done (0.3s)
-- Generating done (0.1s)
-- Build files have been written to: /Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-tuning/build
[ 47%] Built target synthyboy_engine_vital
[ 56%] Built target synthyboy_core
[ 56%] Building CXX object shell-clap/CMakeFiles/synthyboy_static.dir/synthyboy_clap.cpp.o
[ 58%] Building CXX object core/tests/CMakeFiles/synthyboy_test_params.dir/test_params.cpp.o
[ 58%] Building CXX object core/tests/CMakeFiles/synthyboy_test_engine.dir/test_engine.cpp.o
[ 58%] Building CXX object core/tests/CMakeFiles/synthyboy_test_threads.dir/test_threads.cpp.o
[ 59%] Building CXX object core/tests/CMakeFiles/synthyboy_test_rt.dir/test_rt_alloc.cpp.o
[ 59%] Building CXX object core/tests/CMakeFiles/synthyboy_test_state.dir/test_state.cpp.o
[ 59%] Building CXX object core/tests/CMakeFiles/synthyboy_test_preset.dir/test_preset.cpp.o
[ 61%] Built target synthyboy_test_tuning
[ 62%] Building CXX object core/tests/CMakeFiles/synthyboy_test_engine.dir/test_main.cpp.o
[ 63%] Building CXX object core/tests/CMakeFiles/synthyboy_test_rt.dir/test_main.cpp.o
[ 64%] Linking CXX static library libsynthyboy_static.a
[ 64%] Built target synthyboy_static
[ 64%] Building CXX object core/tests/CMakeFiles/synthyboy_test_params.dir/test_main.cpp.o
[ 65%] Building CXX object core/tests/CMakeFiles/synthyboy_test_preset.dir/test_main.cpp.o
[ 66%] Building CXX object core/tests/CMakeFiles/synthyboy_test_state.dir/test_main.cpp.o
[ 66%] Building CXX object core/tests/CMakeFiles/synthyboy_test_threads.dir/test_main.cpp.o
[ 67%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_engine.dir/test_vital_engine.cpp.o
[ 67%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_params.dir/test_vital_params.cpp.o
[ 67%] Linking CXX executable synthyboy_test_rt
[ 67%] Built target synthyboy_test_rt
[ 67%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_engine.dir/__/__/tests/test_main.cpp.o
[ 68%] Linking CXX executable synthyboy_test_params
[ 68%] Linking CXX executable synthyboy_test_preset
[ 68%] Linking CXX executable synthyboy_test_engine
[ 68%] Linking CXX executable synthyboy_test_state
[ 69%] Linking CXX executable synthyboy_test_threads
[ 69%] Built target synthyboy_test_params
[ 69%] Built target synthyboy_test_preset
[ 69%] Built target synthyboy_test_engine
[ 69%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_audio.dir/test_vital_audio.cpp.o
[ 70%] Built target synthyboy_test_state
[ 70%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/test_vital_dsp.cpp.o
[ 70%] Built target synthyboy_test_threads
[ 71%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_audio.dir/__/__/tests/test_main.cpp.o
[ 71%] Building CXX object shell-clap/CMakeFiles/synthyboy_clap.dir/synthyboy_clap.cpp.o
[ 71%] Building CXX object bench/CMakeFiles/synthyboy_bench.dir/bench_engine.cpp.o
[ 72%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_params.dir/__/__/tests/test_main.cpp.o
[ 72%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/effects/compressor_test.cpp.o
[ 73%] Linking CXX executable synthyboy_test_vital_engine
[ 74%] Building CXX object tools/paramdump/CMakeFiles/synthyboy_paramdump.dir/paramdump.cpp.o
[ 75%] Linking CXX shared module synthyboy.clap
[ 75%] Built target synthyboy_test_vital_engine
[ 76%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/effects/delay_test.cpp.o
[ 76%] Built target synthyboy_clap
[ 77%] Building CXX object core/tests/CMakeFiles/synthyboy_test_shell_clap.dir/test_shell_clap.cpp.o
[ 77%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/effects/distortion_test.cpp.o
[ 78%] Linking CXX executable synthyboy_bench
[ 78%] Built target synthyboy_bench
[ 78%] Building CXX object core/tests/CMakeFiles/synthyboy_test_shell_clap.dir/test_main.cpp.o
[ 79%] Linking CXX executable synthyboy_paramdump
[ 79%] Built target synthyboy_paramdump
[ 80%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/effects/phaser_test.cpp.o
[ 81%] Linking CXX executable synthyboy_test_vital_params
[ 81%] Built target synthyboy_test_vital_params
[ 81%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/effects/reverb_test.cpp.o
[ 82%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/filters/comb_filter_test.cpp.o
[ 83%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/filters/dc_filter_test.cpp.o
[ 83%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/filters/decimator_test.cpp.o
[ 84%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/filters/digital_svf_test.cpp.o
[ 84%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/filters/diode_filter_test.cpp.o
[ 85%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/filters/dirty_filter_test.cpp.o
[ 85%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/filters/fir_halfband_decimator_test.cpp.o
[ 86%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/filters/formant_filter_test.cpp.o
[ 87%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/filters/iir_halfband_decimator_test.cpp.o
[ 88%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/filters/ladder_filter_test.cpp.o
[ 88%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/filters/linkwitz_riley_filter_test.cpp.o
[ 89%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/filters/phaser_filter_test.cpp.o
[ 89%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/filters/sallen_key_filter_test.cpp.o
[ 90%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/framework/circular_queue_test.cpp.o
[ 91%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/framework/matrix_test.cpp.o
[ 91%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/framework/poly_values_test.cpp.o
[ 92%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/lookups/wave_frame_test.cpp.o
[ 92%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/modulators/envelope_test.cpp.o
[ 93%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/modulators/random_lfo_test.cpp.o
[ 93%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/modulators/synth_lfo_test.cpp.o
[ 94%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/modulators/trigger_random_test.cpp.o
[ 94%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/note_handler_test.cpp.o
[ 95%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/poly_utils_test.cpp.o
[ 95%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/processor_test.cpp.o
[ 96%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/producers/sample_source_test.cpp.o
[ 96%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/producers/synth_oscillator_test.cpp.o
[ 97%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/utilities/legato_filter_test.cpp.o
[ 98%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/utilities/peak_meter_test.cpp.o
[ 98%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/utilities/portamento_slope_test.cpp.o
[ 99%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/utilities/smooth_value_test.cpp.o
[ 99%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/synthesis/utilities/value_switch_test.cpp.o
[100%] Building CXX object core/engine-vital/tests/CMakeFiles/synthyboy_test_vital_dsp.dir/__/__/tests/test_main.cpp.o
[100%] Linking CXX executable synthyboy_test_vital_dsp
[100%] Built target synthyboy_test_vital_dsp
Test project /Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-tuning/build
      Start  1: synthyboy_test_tuning
 1/13 Test  #1: synthyboy_test_tuning ............   Passed    0.05 sec
      Start  2: synthyboy_test_params
 2/13 Test  #2: synthyboy_test_params ............   Passed    0.37 sec
      Start  3: synthyboy_test_state
 3/13 Test  #3: synthyboy_test_state .............   Passed    2.40 sec
      Start  4: synthyboy_test_preset
 4/13 Test  #4: synthyboy_test_preset ............   Passed    0.08 sec
      Start  5: synthyboy_test_shell_clap
 5/13 Test  #5: synthyboy_test_shell_clap ........   Passed    0.14 sec
      Start  6: synthyboy_test_engine
 6/13 Test  #6: synthyboy_test_engine ............   Passed    3.54 sec
      Start  7: synthyboy_test_rt
 7/13 Test  #7: synthyboy_test_rt ................   Passed    3.53 sec
      Start  8: synthyboy_test_threads
 8/13 Test  #8: synthyboy_test_threads ...........   Passed   12.87 sec
      Start  9: synthyboy_test_vital_engine
 9/13 Test  #9: synthyboy_test_vital_engine ......   Passed    1.82 sec
      Start 10: synthyboy_test_vital_params
10/13 Test #10: synthyboy_test_vital_params ......   Passed    1.37 sec
      Start 11: synthyboy_test_vital_audio
11/13 Test #11: synthyboy_test_vital_audio .......   Passed   23.17 sec
      Start 12: synthyboy_test_vital_dsp
12/13 Test #12: synthyboy_test_vital_dsp .........   Passed    3.15 sec
      Start 13: synthyboy_paramdump
13/13 Test #13: synthyboy_paramdump ..............   Passed    0.06 sec

100% tests passed out of 13

Label Time Summary:
slow    =  23.17 sec*proc (1 test)

Total Test time (real) =  52.55 sec
```

---

## 4. Élőben ellenőrizendők (Live Verification Guide)

Amikor a hangolási réteg a későbbi fázisban összekötésre kerül az `IEngine` és a CLAP/JUCE shellel:
1. **Fájlbetöltés tesztelése:**
   - Helyezzünk el érvényes `.scl`, `.kbm` és `.tun` fájlokat, és a UI menüből töltsük be őket. Ellenőrizendő, hogy a hangmagasságok azonnal átállnak.
   - Helyezzünk el sérült (0 bájtos, nem-numerikus vagy túlzott méretű) fájlokat: ellenőrizendő, hogy nem történik fagyás/crash, és a szintetizátor a korábbi hangolást játssza tovább.
2. **Concert Pitch A4 szabályozó:**
   - Állítsuk a globális A4 potmétert 440 Hz-ről 432 Hz-re vagy 442 Hz-re; ellenőrizzük hangolóval (pl. DAW beépített tuner vagy külső frekvenciamérő), hogy a kimenő frekvencia pontosan arányosan eltolódik.
3. **MTS-ESP kliens kapcsolat:**
   - MTS-ESP master jelenlétében a seam mock alapján a `query128Keys` meghívásával a dinamikus mikrohangolási tábla azonnal érvényesüljön.
