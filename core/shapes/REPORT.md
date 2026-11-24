# SynthyBoy shapes Package - Implementation Report (Q17 GAP Evaluators)

## 1. Summary of Completed Work

Implemented the standalone DSP/data package `core/shapes/` in namespace `synthyboy::shapes` fulfilling all owner decision Q17 items and Serum modulation chapter specifications:

1. **Free-form shape (`ShapeData`, `Point`)**:
   - Point list representation: $x$, $y$, segment curvature `curve` (power scale $-25..25$), `step` (hold mode), `smooth` (cosine S-curve).
   - Point count cap at 128 points (`kMaxPoints = 128`, `kMinPoints = 2`).
   - Validation (`isValid()`): checks finite coordinates, bounds $[0, 1]$, non-decreasing $x$, endpoints at 0.0 and 1.0.
   - Sanitization (`sanitize()`): auto-repairs corrupted/fuzzed point lists, clamps ranges, stably sorts by $x$, sets boundary points.
   - Evaluator (`evaluate(phase)`): real-time safe (zero allocation, zero locks, binary search $O(\log N)$ with $N \le 128$), denormal-safe, finite-guaranteed.
   - Table baking (`bake()`): bakes into 2048-sample preallocated table with fast linear lookup (`evaluateTable`).

2. **LFO Engine Pieces (`PhaseAccumulator`, `LfoEngine`, `RosslerAttractor`, `LorenzAttractor`, `RandomLfoEngine`)**:
   - Phase accumulator with Direction: `forward`, `backward`, `pingPong` (strictly continuous at turnarounds without jumps), `random` (deterministic PRNG direction/phase selection per cycle).
   - Modes: `retrigger`, `freeRun`, `envelope` (one-shot cycle), `sustainEnvelope` (holds at loop point while held), `loopAtPoint`, `loopAndHold`. Supports `mono` (monophonic shared phase) and polyphonic.
   - Beat-unit rate tables: standard 12 beat divisions from 32/1 to 1/64 with `straight` (1.0x), `triplet` (2/3x), `dotted` (1.5x) modifiers. Exact sample period calculations.
   - HOST-sync hook: `syncToHost(ppq, bpm, beats)` anchoring phase deterministically to host song transport position.
   - Fade-in & delay: `LfoFadeDelay` tracking elapsed time post note-on, with zero gain during delay and linear ramp during rise.
   - Smoothing: `LfoSmoother` one-pole slew filter with configurable smooth time in seconds, flush denormals.
   - Chaotic attractors:
     - Rössler attractor (`RosslerAttractor`): 4th-order Runge-Kutta integration, stable and strictly bounded in $[-1.0, 1.0]$, deterministic with seed.
     - Lorenz attractor (`LorenzAttractor`): 4th-order Runge-Kutta integration, bounded in $[-1.0, 1.0]$, deterministic with seed.
   - Random LFO styles: `sampleAndHold`, `sineInterpolate`, `perlin` (Hermite gradient noise), `lorenz`, `rossler`.

3. **Envelope Pieces (`CurveEnvelope`, `MappingCurve`)**:
   - `CurveEnvelope`: curve-segment ADSHR evaluator with `delay`, `attack`, `attackCurve`, `hold`, `decay`, `decayCurve`, `sustain`, `release`, `releaseCurve`. Handles instant transitions (0.0s) and note-off interruptions smoothly from current level without pop/click.
   - `MappingCurve`: Velocity & Note mapping curves (using `ShapeData` point lists with arbitrary curve segments). Includes 7-bit MIDI note and velocity evaluators, plus optional portamento/smoothing for velocity legato.

4. **Per-Slot Remap Curve Evaluator (`RemapCurve`)**:
   - Evaluates modulation matrix remap curves (Q20). Supports unipolar $[0, 1]$ and bipolar $[-1, 1]$ modes, custom multi-point shapes, fallback to `power` curvature parameter, and table baking (default 256 samples).

5. **Build System & Real-Time Safety**:
   - Standalone library `synthyboy_shapes` defined in `core/shapes/CMakeLists.txt`.
   - Exactly ONE line added in `core/CMakeLists.txt` (`add_subdirectory(shapes)`).
   - Real-time safe: zero dynamic memory allocations and zero mutexes/locks in `evaluate()`, `next()`, `process()`.
   - Denormal-safe (`flushDenormal`), NaN/Inf-safe throughout.
   - Sample rates supported: 22050 to 192000 Hz. Block sizes: 1 to 4096 samples.

## 2. Unaffected Areas (What Was NOT Touched)

- No changes to `core/include/synthyboy/params.h` or `docs/params.json` (parameter keys untouched).
- No changes to `core/engine-vital/` or `core/engine-own/`.
- No changes to `shell-juce/` or `shell-clap/`.
- No changes to any existing core tests or engine code.

## 3. Touched Files

- `core/CMakeLists.txt` (added `add_subdirectory(shapes)`)
- `core/shapes/CMakeLists.txt`
- `core/shapes/include/synthyboy/shapes/shape.h`
- `core/shapes/include/synthyboy/shapes/lfo.h`
- `core/shapes/include/synthyboy/shapes/envelope.h`
- `core/shapes/include/synthyboy/shapes/remap.h`
- `core/shapes/include/synthyboy/shapes/shapes.h`
- `core/shapes/src/shape.cpp`
- `core/shapes/src/lfo.cpp`
- `core/shapes/src/envelope.cpp`
- `core/shapes/src/remap.cpp`
- `core/shapes/tests/CMakeLists.txt`
- `core/shapes/tests/test_shape.cpp`
- `core/shapes/tests/test_lfo.cpp`
- `core/shapes/tests/test_envelope.cpp`
- `core/shapes/tests/test_remap.cpp`
- `core/shapes/REPORT.md`

## 4. Gate Results (Verbatim Output)

Command executed:
`cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=OFF && cmake --build build -j8 && ctest --test-dir build`

```text
-- CLAP version: 1.2.7
-- SYNTHYBOY_WITH_VITAL=ON: Vital-derived engine is built (core/engine-vital)
-- Configuring done (0.5s)
-- Generating done (0.1s)
-- Build files have been written to: /Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-shapes/build
[  3%] Built target synthyboy_shapes
[  6%] Built target synthyboy_test_shapes
[ 52%] Built target synthyboy_engine_vital
[ 59%] Built target synthyboy_core
[ 60%] Built target synthyboy_static
[ 61%] Built target synthyboy_test_rt
[ 64%] Built target synthyboy_test_threads
[ 65%] Built target synthyboy_test_params
[ 65%] Built target synthyboy_test_engine
[ 67%] Built target synthyboy_test_preset
[ 69%] Built target synthyboy_test_state
[ 71%] Built target synthyboy_test_vital_engine
[ 73%] Built target synthyboy_test_vital_params
[ 74%] Built target synthyboy_paramdump
[ 75%] Built target synthyboy_bench
[ 76%] Built target synthyboy_clap
[ 78%] Built target synthyboy_test_vital_audio
[ 80%] Built target synthyboy_test_shell_clap
[100%] Built target synthyboy_test_vital_dsp
Test project /Users/szabi/Documents/Projects/Safian/SynthyBoyVST/.claude/worktrees/agy-shapes/build
      Start  1: synthyboy_test_shapes
 1/13 Test  #1: synthyboy_test_shapes ............   Passed    0.03 sec
      Start  2: synthyboy_test_params
 2/13 Test  #2: synthyboy_test_params ............   Passed    0.36 sec
      Start  3: synthyboy_test_state
 3/13 Test  #3: synthyboy_test_state .............   Passed    2.58 sec
      Start  4: synthyboy_test_preset
 4/13 Test  #4: synthyboy_test_preset ............   Passed    0.02 sec
      Start  5: synthyboy_test_shell_clap
 5/13 Test  #5: synthyboy_test_shell_clap ........   Passed    0.07 sec
      Start  6: synthyboy_test_engine
 6/13 Test  #6: synthyboy_test_engine ............   Passed    3.93 sec
      Start  7: synthyboy_test_rt
 7/13 Test  #7: synthyboy_test_rt ................   Passed    3.67 sec
      Start  8: synthyboy_test_threads
 8/13 Test  #8: synthyboy_test_threads ...........   Passed   14.59 sec
      Start  9: synthyboy_test_vital_engine
 9/13 Test  #9: synthyboy_test_vital_engine ......   Passed    2.00 sec
      Start 10: synthyboy_test_vital_params
10/13 Test #10: synthyboy_test_vital_params ......   Passed    1.45 sec
      Start 11: synthyboy_test_vital_audio
11/13 Test #11: synthyboy_test_vital_audio .......   Passed   24.86 sec
      Start 12: synthyboy_test_vital_dsp
12/13 Test #12: synthyboy_test_vital_dsp .........   Passed    3.16 sec
      Start 13: synthyboy_paramdump
13/13 Test #13: synthyboy_paramdump ..............   Passed    0.00 sec

100% tests passed out of 13
```

Detailed test report for `synthyboy_test_shapes`:
```text
[ RUN  ] shape_values_at_known_phases
[  OK  ] shape_values_at_known_phases
[ RUN  ] shape_monotone_curves_stay_monotone
[  OK  ] shape_monotone_curves_stay_monotone
[ RUN  ] shape_edge_points
[  OK  ] shape_edge_points
[ RUN  ] shape_fuzz_point_lists
[  OK  ] shape_fuzz_point_lists
[ RUN  ] lfo_phase_directions_and_ping_pong_continuity
[  OK  ] lfo_phase_directions_and_ping_pong_continuity
[ RUN  ] lfo_modes_retrigger_and_free
[  OK  ] lfo_modes_retrigger_and_free
[ RUN  ] lfo_tempo_sync_period_equals_expected_samples
[  OK  ] lfo_tempo_sync_period_equals_expected_samples
[ RUN  ] lfo_host_sync_hook
[  OK  ] lfo_host_sync_hook
[ RUN  ] lfo_fade_in_and_delay
[  OK  ] lfo_fade_in_and_delay
[ RUN  ] lfo_smoother
[  OK  ] lfo_smoother
[ RUN  ] lfo_rossler_bounded
[  OK  ] lfo_rossler_bounded
[ RUN  ] lfo_lorenz_bounded
[  OK  ] lfo_lorenz_bounded
[ RUN  ] lfo_random_styles
[  OK  ] lfo_random_styles
[ RUN  ] lfo_sample_rates_and_block_sizes
[  OK  ] lfo_sample_rates_and_block_sizes
[ RUN  ] envelope_adshr_curve_evaluator
[  OK  ] envelope_adshr_curve_evaluator
[ RUN  ] envelope_instant_phases_and_interrupted_release
[  OK  ] envelope_instant_phases_and_interrupted_release
[ RUN  ] mapping_curve_velocity_and_note
[  OK  ] mapping_curve_velocity_and_note
[ RUN  ] remap_curve_unipolar_and_bipolar
[  OK  ] remap_curve_unipolar_and_bipolar
18 test case(s), 2035805 check(s), 0 failed case(s), 0.03 s
```

## 5. Live / Manual Verification Items

- When UI shape editor is wired (Phase 4/5), drag gestures should bind to `ShapeData` point coordinates with immediate table rebake.
- Check DAW host transport sync against automation lanes for PPQ anchor during tempo ramps and looped playback.
