# REPORT: SynthyBoy Wavetable Editor Non-UI Data Model and DSP Package

## 1. Overview & Status
- **Package**: `core/wavetable-edit/`
- **Namespace**: `synthyboy::wtedit`
- **Public Header**: `core/include/synthyboy/wavetable_edit.h`
- **Build System**: Per-package `core/wavetable-edit/CMakeLists.txt` integrated via exactly one `add_subdirectory(wavetable-edit)` line in `core/CMakeLists.txt`.
- **Status**: 100% Complete. All operations, data models, DSP functions, import/export, and tests implemented and passing cleanly under `-Wall -Wextra -Wpedantic`.

---

## 2. Implemented Features & Components

### 2.1 Wavetable Data Model
- `Frame`: 2048 floats (`kWaveformSize = 2048`), indexing operators and pointer access.
- `Wavetable`: Collection of 1 to 256 frames (`kMaxFrames = 256`).
- Metadata: `name`, `author`, `comment`, `sampleRate` (default 44100.0).
- Operations: `insertFrame`, `duplicateFrame`, `removeFrame`, `moveFrame`, `initDefault` (standard saw), `clear` (silence). Minimum 1 frame and maximum 256 frames invariants strictly enforced.

### 2.2 Vital Group / Component / Keyframe Model
- `vital::ComponentType`:
  - Signal Sources: `WaveSource`, `LineSource`, `FileSource`.
  - Modifiers: `PhaseModifier` (Normal, EvenOdd, Harmonic, HarmonicEvenOdd, Clear), `WaveWindowModifier` (Cos, HalfSin, Linear, Square, Wiggle), `FrequencyFilterModifier` (LowPass, HighPass, BandPass, Comb), `SlewLimitModifier`, `WaveFoldModifier`, `WaveWarpModifier` (horizontal & vertical power warping).
- `vital::InterpolationStyle`: `None`, `Linear`, `Cubic` (Catmull-Rom), `Spectral`.
- `vital::Keyframe`: Position in [0.0, 255.0], waveform data, and modifier settings.
- `vital::Group`: Ordered component chain rendering frames into `Wavetable` or synchronizing keyframes from existing wavetables.

### 2.3 Own Radix-2 FFT
- Custom in-place / out-of-place Cooley-Tukey Radix-2 FFT in `src/fft.cpp`.
- Supports any power-of-two size (specifically 2048, 1024, 512, 256).
- Bit-reversal permutation and trigonometric twiddle recurrence.
- `Radix2Fft::forward` and `Radix2Fft::inverse`.
- `Radix2Fft::realToPolar` and `Radix2Fft::polarToReal` (conjugate symmetric real spectrum reconstruction).
- Verified round-trip error < 1e-5.

### 2.4 Frame Operations (Time Domain, Spectral, Morph, and Drawing Tools)
Every operation documented with dedicated documentation comments:
- **Normalisation**: `normalise` (peak <= 1.0, idempotent), `normaliseEach`, `normaliseSame`.
- **DC Removal**: `removeDc` (subtracts mean sample, mean == 0, idempotent), `removeDcAll`.
- **Inversion & Reversal**: `flipVertical` (y = -y), `flipVerticalAll`, `flipHorizontal` (reverse time order), `flipHorizontalAll`.
- **Edge Fading & Windowing**: `fadeEdges` (grid-dependent window), `fadeEdgesAll`, `crossfadeEdges` (seamless looping across boundaries), `crossfadeEdgesAll`, `crossfadeEdges16Samples`, `crossfadeEdges16SamplesAll`.
- **Zero Crossing**: `shiftToZeroCrossing` (positive-going zero crossing aligned to sample 0), `shiftToZeroCrossingAll`.
- **Frame Management**: `insertFrame`, `removeFrame`, `removeRange`, `removeBeginningToSelected`, `removeSelectedToEnd`, `removeAllExceptSelected`, `reduceTo` (frame thinning), `resizeTablesHalf` (doubles frame count by splitting cycles), `resizeTablesDouble` (halves frame count by merging pairs).
- **Modulation & Synthesis**: `createPwmFromFrame` (sweeps duty cycle across 256 frames), `sampleRedux` (lo-fi sample rate reduction), `smoothTimeDomain` (moving average), `bendTimeDomain` (phase curvature warp), `fmBetweenFrames` (phase modulation), `amBetweenFrames` (amplitude modulation).
- **Spectral Filtering & Operations**: `filterLowPass`, `removeFundamental`, `removeLowSpectra`, `removeLowPhases`, `nudgeFundamentalPhaseTo50Percent`, `setSpectraThisFrameToAll`, `setPhasesThisFrameToAll`, `setSpectraFromOther`, `setPhasesFromOther`, `subtractSpectraFromOther`, `blurSpectraAdjacentBins`, `blurPhasesAdjacentBins`, `blurSpectraAdjacentFrames`, `blurPhasesAdjacentFrames`.
- **Morph / Interpolation**: `interpolateBetweenKeyframes` supporting `Crossfade`, `Spectral`, `SpectralZeroFundPhase`, `SpectralZeroAllPhases`, and `removeMorphTables`.
- **Spectral Sorting**: `sortFrames` supporting `PeakSpect`, `AverageSpect`, `PeakAmount`, `NumWithSpect`, `HighestWithSpect`, `FundamentalAmount`, `Randomize`, `Reverse`.
- **FFT Area Menu Operations**: `fftClearAll`, `fftClearHf`, `fftClearLf`, `fftGenerateSaw`, `fftRandomizeLowBins` (with/without half), `fftRandomizeAll`, `fftCreateRandomSeriesGaps`, `fftProgressiveFade`, `fftShiftOctaveUp`, `fftShiftOctaveDown`, `fftRepeatBinGroup`, `fftScaleFreqValuesByBinIndex`, `fftSetHarmonic`.
- **Drawing Tools**: `applyDrawingTool` supporting `FlatLine`, `Sine`, `SlopeUp`, `SlopeDown`, `CurveUp`, `CurveDown`, `HalfSinePeak`, `HalfSineValley`, `InterpolateLinear`, `InterpolateCurved`, `Nudge`, `Noise`, `Mirror`.

### 2.5 Undo / Redo Command Stack
- `UndoStack`: Bounded history (default 50 states, configurable max depth).
- Bit-exact state restoration (`memcmp`-identical float restoration).
- `push`, `undo`, `redo`, `canUndo`, `canRedo`, `clear`.

### 2.6 Audio Import & Resampling
- Formats: 16-bit PCM integer, 24-bit PCM integer, 32-bit PCM integer, 32-bit IEEE float.
- Channels: Mono and Stereo channel selection (Left = 0 for Osc 1/3, Right = 1 for Osc 2).
- Slicing modes:
  - `FixedSingleCycle`: first cycle / 2048 samples.
  - `ByFrameSize`: fixed sample length slicing (32..9999).
  - `PitchDetectZeroSnap`: autocorrelation / NSDF pitch detection with positive zero-crossing snapping.
  - `PitchDetectFollow`: dynamic pitch tracking cycle-by-cycle.
  - `PitchAverage`: global fundamental pitch estimation.
  - `Fft256`, `Fft512`, `Fft1024`, `Fft2048`: spectral windowing and harmonic resynthesis.
- Cubic Hermite resampler (`resampleBuffer`) converting arbitrary input lengths to 2048 samples.
- Fuzz testing against malformed/truncated headers, corrupted chunk sizes, invalid bit depths.

### 2.7 Image Import (Additive Synthesis)
- Raw RGBA8888 buffer or 8-bit Grayscale buffer.
- Width maps to frame count (<= 256).
- Height maps to harmonic bins (1..min(height, 1024)).
- Luminance maps to harmonic amplitude.
- Reconstructs frames via inverse FFT with peak normalized <= 1.0.

### 2.8 Audio Export & Serum Chunk
- `Wav16Bit`: 16-bit PCM mono WAV (44100 Hz, 2048 samples/frame).
- `Wav32BitFloat`: 32-bit IEEE float mono WAV (44100 Hz, 2048 samples/frame).
- `SingleCycle`: single 2048-sample WAV export.
- Serum 'clm ' Chunk: Includes RIFF chunk `"clm "` with `"<!>2048 10000000 wavetable (created in SynthyBoy)\0"`.
- **Documented Gap**: Serum manual (pages 285-286) references "extra header information" without publishing the binary specification of the 'clm ' chunk. We implement the standard RIFF wavetable format with the widely compatible `clm ` chunk.

### 2.9 Standalone DSP / Real-Time Safe Player
- `PlayerParams`: `framePosition` (smooth morphing), `frequency` (20..24000 Hz), `phaseOffset` (0..1), `gain`.
- `WavetablePlayer`:
  - Real-time safe: Zero heap allocations in `process()`, no mutexes/locks.
  - Sample rates tested: 22050 Hz to 192000 Hz.
  - Block sizes tested: 1 to 4096 samples.
  - Denormal protection (`+ 1e-30f - 1e-30f`).
  - NaN / Inf protection on parameters and outputs.

---

## 3. Touched Files
- `core/CMakeLists.txt` (added exactly 1 line: `add_subdirectory(wavetable-edit)`)
- `core/include/synthyboy/wavetable_edit.h`
- `core/wavetable-edit/CMakeLists.txt`
- `core/wavetable-edit/src/fft.h`
- `core/wavetable-edit/src/fft.cpp`
- `core/wavetable-edit/src/wavetable.cpp`
- `core/wavetable-edit/src/drawing_tools.cpp`
- `core/wavetable-edit/src/frame_ops.cpp`
- `core/wavetable-edit/src/vital_model.cpp`
- `core/wavetable-edit/src/undo.cpp`
- `core/wavetable-edit/src/audio_import.cpp`
- `core/wavetable-edit/src/image_import.cpp`
- `core/wavetable-edit/src/wav_export.cpp`
- `core/wavetable-edit/src/player.cpp`
- `core/wavetable-edit/tests/test_wtedit.cpp`
- `core/wavetable-edit/REPORT.md`

Files untouched: All other core files, engine implementations, preset handlers, and docs.

---

## 4. Gate Verification & Test Outputs

### Unit Test Execution: `synthyboy_test_wtedit`
```
[ RUN  ] fft_round_trip
[  OK  ] fft_round_trip
[ RUN  ] wavetable_model_invariants
[  OK  ] wavetable_model_invariants
[ RUN  ] frame_ops_invariants
[  OK  ] frame_ops_invariants
[ RUN  ] frame_ops_all_and_ranges
[  OK  ] frame_ops_all_and_ranges
[ RUN  ] table_resize_ops
[  OK  ] table_resize_ops
[ RUN  ] drawing_tools
[  OK  ] drawing_tools
[ RUN  ] fft_spectral_ops
[  OK  ] fft_spectral_ops
[ RUN  ] cross_frame_spectral_and_morph_ops
[  OK  ] cross_frame_spectral_and_morph_ops
[ RUN  ] vital_group_component_model
[  OK  ] vital_group_component_model
[ RUN  ] undo_redo_bit_exact
[  OK  ] undo_redo_bit_exact
[ RUN  ] wav_export_import_roundtrip
[  OK  ] wav_export_import_roundtrip
[ RUN  ] audio_import_slicing_modes
[  OK  ] audio_import_slicing_modes
[ RUN  ] malformed_wav_fuzz
[  OK  ] malformed_wav_fuzz
[ RUN  ] image_import_invariants
[  OK  ] image_import_invariants
[ RUN  ] realtime_player_invariants
[  OK  ] realtime_player_invariants
15 test case(s), 142913 check(s), 0 failed case(s), 0.38 s
```

### Full CTest Suite Execution:
```
Test project /Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-wtedit/build
      Start  1: synthyboy_test_wtedit
 1/13 Test  #1: synthyboy_test_wtedit ............   Passed    0.38 sec
      Start  2: synthyboy_test_params
 2/13 Test  #2: synthyboy_test_params ............   Passed    0.51 sec
      Start  3: synthyboy_test_state
 3/13 Test  #3: synthyboy_test_state .............   Passed    2.99 sec
      Start  4: synthyboy_test_preset
 4/13 Test  #4: synthyboy_test_preset ............   Passed    0.18 sec
      Start  5: synthyboy_test_shell_clap
 5/13 Test  #5: synthyboy_test_shell_clap ........   Passed    0.25 sec
      Start  6: synthyboy_test_engine
 6/13 Test  #6: synthyboy_test_engine ............   Passed    4.38 sec
      Start  7: synthyboy_test_rt
 7/13 Test  #7: synthyboy_test_rt ................   Passed    3.86 sec
      Start  8: synthyboy_test_threads
 8/13 Test  #8: synthyboy_test_threads ...........   Passed   14.12 sec
      Start  9: synthyboy_test_vital_engine
 9/13 Test  #9: synthyboy_test_vital_engine ......   Passed    2.12 sec
      Start 10: synthyboy_test_vital_params
10/13 Test #10: synthyboy_test_vital_params ......   Passed    1.54 sec
      Start 11: synthyboy_test_vital_audio
11/13 Test #11: synthyboy_test_vital_audio .......   Passed   25.29 sec
      Start 12: synthyboy_test_vital_dsp
12/13 Test #12: synthyboy_test_vital_dsp .........   Passed    3.59 sec
      Start 13: synthyboy_paramdump
13/13 Test #13: synthyboy_paramdump ..............   Passed    0.05 sec

100% tests passed out of 13
```

---

## 5. Items for Live / UI Verification
1. Integration of `vital::Group` and `Wavetable` into the UI canvas touch-drawing handlers.
2. File picker binding for OS share-sheet / drag-and-drop audio and PNG files.
3. Audio engine thread-safe wavetable pointer exchange on block boundaries when editing completes.
