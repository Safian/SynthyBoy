# REPORT: Serum FX 'Bode' and 'Convolve' Package (`core/fx-serum-b`)

## 1. Pontonként kész / mi maradt
- [x] **Radix-2 FFT (`fft.h`, `fft.cpp`):**
  - Kész: saját, harmadik féltől független in-place radix-2 Cooley-Tukey FFT és IFFT.
  - Valós idejű biztonság: audio szálon 0 memóriafoglalás / zár (bit-reversal tábla és twiddle faktorok előre kiszámítva az `init()` során).
- [x] **WAV olvasó & író (`wav_reader.h`, `wav_reader.cpp`):**
  - Kész: beépített RIFF WAV olvasó és teszt-író.
  - Támogatott formátumok: 16-bit, 24-bit, 32-bit előjeles PCM és 32-bit IEEE float (standard tag 3 és extensible GUID).
- [x] **Bode frekvenciaeltoló (`bode.h`, `bode.cpp`):**
  - Kész: Hilbert-transzformáción alapuló single-sideband shifter a Serum kézikönyv 161. oldala és `docs/ui-design/08-effects.md` szerint.
  - Vezérlők: `monoInput`, `shift`, `range`, `dir`, `width`, `delay`, `bpm`, `tempoBpm`, `delayBeats`, `feedback`, `balance`, `blur`, `mix`, `level`, `retrig()`.
  - 129-pontos Blackman-ablakozott FIR Hilbert-szűrő lineáris fázissal és >70 dB elnyomással a nem kívánt oldalsávra.
  - Valós idejű biztonság: 0 allokáció `process()`-ben, denormális / inf / NaN védelem, visszacsatolás puha korlátozása (soft clip).
- [x] **Convolve konvolúciós zengető / IR processzor (`convolve.h`, `convolve.cpp`):**
  - Kész: particionált FFT konvolúció (uniform partíciók: 128 pontos közvetlen FIR fej + uniform 128 pontos partíciók 256 pontos radix-2 FFT-vel).
  - Valódi nulla késleltetés (`latency() == 0`).
  - Vezérlők: `size` (időnyújtás/zsugorítás), `tone` (spektrális tilt szűrő), `minimumPhase` ($\phi$ MIN minimum fázisú konverzió cepsztrum módszerrel), `preDelay`, `bpm`, `tempoBpm`, `preDelayBeats`, `attack`, `decay`, `damp`, `irGain`, `mix`, `level`.
  - IR betöltés: `loadIr(const float*, int frames, int channels, double irRate)` (non-RT).
  - IR hosszkorlát dokumentálva: `kMaxIrFrames = 524288` (~10.9 mp 48 kHz-en).
  - Minőségi sinc ablakozott mintavételezési frekvencia konverzió (`irRate` -> `sampleRate`).
  - Blokkméret-függetlenség: azonos kimenet 1, 64, 512 méretű blokkokkal 1e-6 pontossággal.
- [x] **Egységtesztek (`tests/`):**
  - Kész: 20 teszteset, 28 527 ellenőrzés, 0 hiba.
  - Tartalmazza: Bode szinusz eltolás FFT csúcs ellenőrzés (1200 Hz és 700 Hz), sztereó ellentétes eltolás, Convolve egységimpulzus = identitás, késleltetett impulzus = tiszta késleltetés, IR frekvenciakonverzió (44.1k -> 48k), blokkméret-függetlenség mindkét modulra, minimum fázis, WAV olvasás minden bitmélységre, NaN/inf védelem.
- [x] **Mi maradt:** Semmi; a specifikációban megadott összes követelmény hiánytalanul teljesült.

## 2. Érintett fájlok
### Módosított meglévő fájlok:
- `core/CMakeLists.txt` (pontosan egy sor hozzáadva: `add_subdirectory(fx-serum-b)`)

### Újonnan létrehozott csomagfájlok (`core/fx-serum-b/`):
- `core/fx-serum-b/CMakeLists.txt`
- `core/fx-serum-b/fft.h`
- `core/fx-serum-b/fft.cpp`
- `core/fx-serum-b/wav_reader.h`
- `core/fx-serum-b/wav_reader.cpp`
- `core/fx-serum-b/bode.h`
- `core/fx-serum-b/bode.cpp`
- `core/fx-serum-b/convolve.h`
- `core/fx-serum-b/convolve.cpp`
- `core/fx-serum-b/README.md`
- `core/fx-serum-b/REPORT.md`
- `core/fx-serum-b/tests/CMakeLists.txt`
- `core/fx-serum-b/tests/test_fft.cpp`
- `core/fx-serum-b/tests/test_wav_reader.cpp`
- `core/fx-serum-b/tests/test_bode.cpp`
- `core/fx-serum-b/tests/test_convolve.cpp`

## 3. A kapuk kimenete szó szerint

### Build kapu
`cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=OFF && cmake --build build -j8`
```
-- CLAP version: 1.2.7
-- SYNTHYBOY_WITH_VITAL=ON: Vital-derived engine is built (core/engine-vital)
-- Configuring done (0.5s)
-- Generating done (0.1s)
-- Build files have been written to: /Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-fxB/build
[ 46%] Built target synthyboy_engine_vital
[ 53%] Built target synthyboy_core
[ 54%] Built target synthyboy_test_threads
[ 55%] Built target synthyboy_test_params
[ 57%] Built target synthyboy_static
[ 57%] Built target synthyboy_test_engine
[ 58%] Built target synthyboy_test_rt
[ 60%] Built target synthyboy_test_preset
[ 62%] Built target synthyboy_test_state
[ 63%] Built target synthyboy_clap
[ 64%] Built target synthyboy_bench
[ 65%] Built target synthyboy_paramdump
[ 67%] Built target synthyboy_test_vital_params
[ 69%] Built target synthyboy_test_vital_audio
[ 71%] Built target synthyboy_test_vital_engine
[ 73%] Built target synthyboy_test_shell_clap
[ 93%] Built target synthyboy_test_vital_dsp
[ 96%] Built target synthyboy_fx_serum_b
[100%] Built target synthyboy_test_fx_serum_b
```

### Csomagteszt kapu
`./build/core/fx-serum-b/tests/synthyboy_test_fx_serum_b`
```
[ RUN  ] fft_impulse_roundtrip
[  OK  ] fft_impulse_roundtrip
[ RUN  ] fft_sine_peak
[  OK  ] fft_sine_peak
[ RUN  ] wav_reader_roundtrip_16bit
[  OK  ] wav_reader_roundtrip_16bit
[ RUN  ] wav_reader_roundtrip_24bit
[  OK  ] wav_reader_roundtrip_24bit
[ RUN  ] wav_reader_roundtrip_32bit_pcm
[  OK  ] wav_reader_roundtrip_32bit_pcm
[ RUN  ] wav_reader_roundtrip_32bit_float
[  OK  ] wav_reader_roundtrip_32bit_float
[ RUN  ] wav_reader_invalid_header
[  OK  ] wav_reader_invalid_header
[ RUN  ] bode_shift_sine_up_hits_fft_peak
[  OK  ] bode_shift_sine_up_hits_fft_peak
[ RUN  ] bode_shift_sine_down_hits_fft_peak
[  OK  ] bode_shift_sine_down_hits_fft_peak
[ RUN  ] bode_stereo_dir_center_opposite_shifts
[  OK  ] bode_stereo_dir_center_opposite_shifts
[ RUN  ] bode_mono_input
[  OK  ] bode_mono_input
[ RUN  ] bode_block_size_independence
[  OK  ] bode_block_size_independence
[ RUN  ] bode_nan_inf_safety
[  OK  ] bode_nan_inf_safety
[ RUN  ] convolve_unit_impulse_equals_identity
[  OK  ] convolve_unit_impulse_equals_identity
[ RUN  ] convolve_delayed_impulse_equals_delay
[  OK  ] convolve_delayed_impulse_equals_delay
[ RUN  ] convolve_ir_rate_conversion
[  OK  ] convolve_ir_rate_conversion
[ RUN  ] convolve_block_size_independence
[  OK  ] convolve_block_size_independence
[ RUN  ] convolve_minimum_phase
[  OK  ] convolve_minimum_phase
[ RUN  ] convolve_predelay
[  OK  ] convolve_predelay
[ RUN  ] convolve_nan_inf_safety
[  OK  ] convolve_nan_inf_safety
20 test case(s), 28527 check(s), 0 failed case(s), 0.01 s
```

### CTest teljes projektkapu
`ctest --test-dir build`
```
Test project /Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-fxB/build
      Start  1: synthyboy_test_params
 1/13 Test  #1: synthyboy_test_params ............   Passed    0.35 sec
      Start  2: synthyboy_test_state
 2/13 Test  #2: synthyboy_test_state .............   Passed    2.61 sec
      Start  3: synthyboy_test_preset
 3/13 Test  #3: synthyboy_test_preset ............   Passed    0.01 sec
      Start  4: synthyboy_test_shell_clap
 4/13 Test  #4: synthyboy_test_shell_clap ........   Passed    0.07 sec
      Start  5: synthyboy_test_engine
 5/13 Test  #5: synthyboy_test_engine ............   Passed    4.01 sec
      Start  6: synthyboy_test_rt
 6/13 Test  #6: synthyboy_test_rt ................   Passed    3.74 sec
      Start  7: synthyboy_test_threads
 7/13 Test  #7: synthyboy_test_threads ...........   Passed   14.01 sec
      Start  8: synthyboy_test_vital_engine
 8/13 Test  #8: synthyboy_test_vital_engine ......   Passed    1.91 sec
      Start  9: synthyboy_test_vital_params
 9/13 Test  #9: synthyboy_test_vital_params ......   Passed    1.36 sec
      Start 10: synthyboy_test_vital_audio
10/13 Test #10: synthyboy_test_vital_audio .......   Passed   24.87 sec
      Start 11: synthyboy_test_vital_dsp
11/13 Test #11: synthyboy_test_vital_dsp .........   Passed    3.55 sec
      Start 12: synthyboy_test_fx_serum_b
12/13 Test #12: synthyboy_test_fx_serum_b ........   Passed    0.01 sec
      Start 13: synthyboy_paramdump
13/13 Test #13: synthyboy_paramdump ..............   Passed    0.00 sec

100% tests passed out of 13

Label Time Summary:
slow    =  24.87 sec*proc (1 test)

Total Test time (real) =  56.51 sec
```

## 4. Élőben ellenőrizendők
1. DAW / CLAP host környezetben Serum 2 presetek összehasonlító meghallgatása Convolve és Bode beállításokkal.
2. VST / CLAP burkoló paraméterkötések bekötése a fő szál által, amint a paramétertáblába bekerülnek a Serum FX kulcsok.
