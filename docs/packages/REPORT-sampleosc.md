# REPORT: Sample Oscillator Package (core/sample-osc)

## 1. Elkészült feladatok (Pontonként kész / mi maradt)
- [x] **Sample data representation**: `SampleView` könnyűsúlyú, non-allocating, nem birtokló nézet mono/sztereó lebegőpontos mintákhoz, bármilyen mintavételezési frekvenciára.
- [x] **Non-RT SampleData tároló**: Planar csatorna bufferek, move-szemantika, automatikus oktáv mipmap piramis építés (0..7 szintek) Kaiser-ablakozott sinc FIR szűrővel ($\beta = 8.0, 101$ tap), > 55 dB alias-elnyomás hangmagasság-emeléskor.
- [x] **UI áttekintő hullámforma (peak/overview)**: `computeOverview` és `computeOverviewStereo` $N$ pixel szélességre (min/max burok per oszlop).
- [x] **WAV olvasó**: 16/24/32 bites signed int PCM és 32 bites IEEE float támogatás, mono és sztereó, szigorú biztonsági korlátokkal (max 64M frame, max 512 MB, fuzzer/garbage bemenetekre garantáltan hibatűrő).
- [x] **Metaadat-olvasás**: Alaphang (root key) és finomhangolás fájlnév végződésből (pl. `"Morning Bass F2.wav"`, `"Ashes Piano Eb3.wav"`, C-1..G9), WAV `'inst'` chunkból és `'smpl'` sampler loop markerekből.
- [x] **Polyphony-ready SampleVoice**: Valós idejű biztonság (zero allocation/lock a hangszálon), C++17, denormal-safe, NaN/inf-safe.
- [x] **Hangmagasság és transzponálás**: Root key, coarse tune (félhangok), fine tune (centek), pitch tracking toggle.
- [x] **Hurok módok (Loop modes)**: `none` (one-shot), `forward` (hurok crossfade-del), `pingPong` (kétirányú oda-vissza), `reverse` (folyamatos visszajátszás hurok).
- [x] **Keresztkeverés (Crossfade)**: Equal-power simítás a hurokhatárokon, kattanásmentes átmenet és garantált deriváltkorlát ($\Delta s < 0.03$).
- [x] **Interpoláció és minőségi szintek**: 512 fázisú előre számolt windowed-sinc táblázatok, DC egységnyi erősítéssel:
  - Low tier: 4-tap ablakozott sinc
  - Medium tier: 8-tap ablakozott sinc
  - High tier: 16-tap ablakozott sinc folytonos mipmap rétegközi crossfade-del.
- [x] **Anti-click indítás és leállítás**: ~1.5 ms rámpa bekapcsoláskor és ~2.0 ms rámpa kikapcsoláskor.
- [x] **Unison hookok**: Hangonkénti pitch ratio szorzó, indítási fáziseltolás, sztereó panoráma eltolás és jelszint szorzó.
- [x] **Wavetable-szerű mapping**: Owner döntés szerint kizárva (out of scope).
- [x] **Mi maradt**: Semmi. A feladat 100%-ban elkészült.

## 2. Érintett fájlok
### Módosított meglévő fájlok:
- `core/CMakeLists.txt` (pontosan EGYETLEN `add_subdirectory(sample-osc)` sor hozzáadva a végén)

### Új fájlok a csomagban (`core/sample-osc/`):
- `core/sample-osc/CMakeLists.txt`: Csomag CMake konfiguráció (`synthyboy_sampleosc` statikus könyvtár és `synthyboy_test_sampleosc` teszt célpont)
- `core/sample-osc/include/synthyboy/sampleosc/sample_types.h`: Típusok, enumok (`LoopMode`, `QualityTier`), `SampleOscParams`, `SampleView`, `OverviewPoint`
- `core/sample-osc/include/synthyboy/sampleosc/sample_data.h`: `SampleData` osztály fejléce
- `core/sample-osc/include/synthyboy/sampleosc/sample_voice.h`: `SampleVoice` osztály és `SincTables` fejléce
- `core/sample-osc/include/synthyboy/sampleosc/wav_reader.h`: WAV olvasó és fájlnév-elemző fejléce
- `core/sample-osc/src/sample_data.cpp`: Mipmap generálás és áttekintő hullámforma számítás implementációja
- `core/sample-osc/src/sample_voice.cpp`: Hangszál szintetizáló DSP, interpoláció, huroklogika, anti-click és unison implementációja
- `core/sample-osc/src/wav_reader.cpp`: Robusztus WAV dekódoló és formátumellenőrző implementációja
- `core/sample-osc/tests/test_sample_osc.cpp`: Teljes tesztcsomag (12 teszteset, 5343 ellenőrzés)
- `core/sample-osc/REPORT.md`: Részletes csomagriport

## 3. Kapuk kimenete szó szerint

### A) Build (`cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=OFF && cmake --build build -j8`)
```
-- CLAP version: 1.2.7
-- SYNTHYBOY_WITH_VITAL=ON: Vital-derived engine is built (core/engine-vital)
-- Configuring done (0.3s)
-- Generating done (0.1s)
-- Build files have been written to: /Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-sampleosc/build
[  3%] Built target synthyboy_sampleosc
[ 50%] Built target synthyboy_engine_vital
[ 57%] Built target synthyboy_core
[ 60%] Built target synthyboy_static
[ 60%] Built target synthyboy_test_threads
[ 63%] Built target synthyboy_test_vital_engine
[ 63%] Built target synthyboy_test_preset
[ 68%] Built target synthyboy_test_state
[ 68%] Built target synthyboy_test_params
[ 68%] Built target synthyboy_test_engine
[ 70%] Built target synthyboy_test_rt
[ 71%] Built target synthyboy_paramdump
[ 73%] Built target synthyboy_clap
[ 73%] Built target synthyboy_test_vital_audio
[ 74%] Built target synthyboy_test_shell_clap
[ 75%] Built target synthyboy_bench
[ 77%] Built target synthyboy_test_vital_params
[ 99%] Built target synthyboy_test_vital_dsp
[100%] Built target synthyboy_test_sampleosc
```
Figyelmeztetések száma: 0 (`-Wall -Wextra -Wpedantic` tiszta).

### B) Saját tesztek futtatása (`./build/core/sample-osc/synthyboy_test_sampleosc`)
```
[ RUN  ] sampleosc_wav_filename_note_parsing
[  OK  ] sampleosc_wav_filename_note_parsing
[ RUN  ] sampleosc_wav_reader_formats
[  OK  ] sampleosc_wav_reader_formats
[ RUN  ] sampleosc_wav_reader_garbage_inputs
[  OK  ] sampleosc_wav_reader_garbage_inputs
[ RUN  ] sampleosc_pitch_accuracy_5_octaves
[  OK  ] sampleosc_pitch_accuracy_5_octaves
[ RUN  ] sampleosc_loop_continuity_no_click
[  OK  ] sampleosc_loop_continuity_no_click
[ RUN  ] sampleosc_ping_pong_symmetry
[  OK  ] sampleosc_ping_pong_symmetry
[ RUN  ] sampleosc_reverse_equals_reversed_forward
[  OK  ] sampleosc_reverse_equals_reversed_forward
[ RUN  ] sampleosc_aliasing_sanity_noise_pitch_up
[  OK  ] sampleosc_aliasing_sanity_noise_pitch_up
[ RUN  ] sampleosc_overview_computation
[  OK  ] sampleosc_overview_computation
[ RUN  ] sampleosc_unison_and_denormal_safety
[  OK  ] sampleosc_unison_and_denormal_safety
[ RUN  ] sampleosc_samplerates_and_blocksizes
[  OK  ] sampleosc_samplerates_and_blocksizes
[ RUN  ] sampleosc_quality_tiers
[  OK  ] sampleosc_quality_tiers
12 test case(s), 5343 check(s), 0 failed case(s), 0.01 s
```

### C) Teljes CTest kapu (`ctest --test-dir build`)
```
Test project /Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-sampleosc/build
      Start  1: synthyboy_test_params
 1/13 Test  #1: synthyboy_test_params ............   Passed    0.33 sec
      Start  2: synthyboy_test_state
 2/13 Test  #2: synthyboy_test_state .............   Passed    2.62 sec
      Start  3: synthyboy_test_preset
 3/13 Test  #3: synthyboy_test_preset ............   Passed    0.02 sec
      Start  4: synthyboy_test_shell_clap
 4/13 Test  #4: synthyboy_test_shell_clap ........   Passed    0.07 sec
      Start  5: synthyboy_test_engine
 5/13 Test  #5: synthyboy_test_engine ............   Passed    4.25 sec
      Start  6: synthyboy_test_rt
 6/13 Test  #6: synthyboy_test_rt ................   Passed    3.79 sec
      Start  7: synthyboy_test_threads
 7/13 Test  #7: synthyboy_test_threads ...........   Passed   14.28 sec
      Start  8: synthyboy_test_vital_engine
 8/13 Test  #8: synthyboy_test_vital_engine ......   Passed    2.08 sec
      Start  9: synthyboy_test_vital_params
 9/13 Test  #9: synthyboy_test_vital_params ......   Passed    1.47 sec
      Start 10: synthyboy_test_vital_audio
10/13 Test #10: synthyboy_test_vital_audio .......   Passed   25.21 sec
      Start 11: synthyboy_test_vital_dsp
11/13 Test #11: synthyboy_test_vital_dsp .........   Passed    3.21 sec
      Start 12: synthyboy_test_sampleosc
12/13 Test #12: synthyboy_test_sampleosc .........   Passed    0.01 sec
      Start 13: synthyboy_paramdump
13/13 Test #13: synthyboy_paramdump ..............   Passed    0.00 sec

100% tests passed out of 13
```

## 4. Élőben ellenőrizendők
- UI oldali vizuális tesztelés, amikor a mintaoszcillátor nézet és a hullámforma komponens összekötésre kerül az `OverviewPoint` adatokkal.
- A főkötelék (main thread / engine adapter) összefűzése az `IEngine` és a `paramTable` paraméter kulcsokkal.
