# Test recipes

- **Test first, and prove it fails:** every change lands as a pair: the test that fails on the
  old code (compile-fail counts, say so), then the change. A test that passes before the fix
  proves nothing.
- **Signals must not hide the defect:** no signal whose zero crossings or whole periods line up
  with the defect (a chirp's zero crossings fooled the granular loop test twice); the defect's
  effect must be at least 10x the threshold.
- **Ratios are not proofs:** "RMS ratio < 0.97" depended on phases; prove silence with exact
  zeros. (own_spectral_a_waiting_voice_is_silent, 2026-11-21)
- **Read inside the render:** a test read 64 frames past its render end and was flaky with heap
  garbage; run suspicious tests with `MallocScribble=1`.
- **Checksums:** `*-CHECKSUM` lines are compared before/after by the build worker; a change that
  must not change sound must leave every line identical; intended changes list their lines.
- **Bit identity across compiles:** `-ffp-contract=off` (see platform file).
- **Batch builds:** workers write, one build worker builds the merged tree once; phase A (old
  code, new tests must fail) and phase B (all pass).
- **Never memcmp a struct:** padding bytes hold heap garbage (voice_opt compared `VoiceCtl` with its 3 padding bytes and was red only after an unrelated layout change); compare the fields.
- **A test that has never been built is not a test:** the agy branches of 2026-11-21 had a wrong include (`synthyboy/param_ids.h`) and a doubly declared member (`fx_oversampler.h`); both showed only in the first build.
- **A TSan report is a real race:** the FX builder thread read the audio thread's slot module pointer (`FxPool::typeChanged` vs `FxChain::configureSlot`); it showed in 2 of 20 `stub_host` runs but in 5 of 5 of a test that re-sets the same FX type key while the audio thread processes. Hammer the control path in the test, do not wait for the flaky run. (test: `synthyboy_test_own_fx_worker_race`, 2026-11-21)

- **LFO loop migration:** To check continuity, compare the authored values immediately before and after the engine's jump. In the own engine the phase parameter sets the return position for Loop at point and the end of the held interval for Loop and hold. The stored shape editor loop marker is independent. Envelopes have no repeated interval. Source: `core/engine-own/env_lfo/lfo.h` (`LfoCore::advance` and `position`). Test: `core/tests/test_lfo_seam_migration.cpp`, including the first engine wrap after loading a legacy state. With both editor links enabled, verify the full-cycle endpoints and the internal return ordinate after editing either an overlapping or an independent endpoint. Test: `shell-mpc/overlay/tests/lfo_ui_host.cpp`, `testEditorMarker`.

- **Chorus phase-table bounds** (tested: `core/tests/test_own_chorus_lanes.cpp`, Apple arm64).
  Include large negative phases and non-finite values in the four-lane comparison.
  The scalar and vector sine helpers return zero for integral phases of magnitude
  at least 2²³ and for infinity, and propagate NaN before converting a table index.
- **Fast exp2 non-finite input** (tested: `fast_exp2_preserves_nan_and_saturates_infinities`).
  Run with `-fsanitize=float-cast-overflow` to catch a NaN converted to an integer
  exponent even when the numerical result happens to propagate NaN.

## Compiled content publication

`ContentHub` hands custom LFO shapes and modulation curves to one audio consumer per engine.
An empty hub returns an empty snapshot. After publication the renderer keeps its slot until
the next acquisition that advances it. The reader does one relaxed load and, when an update
is pending, one acquire-release exchange. The publisher reclaims returned slots outside the
audio callback. A generation travels with its own data and invalidates the instance's matrix
and voice caches. Source: `core/engine-own/voice/own_content.h` and `core/engine-own/own_engine.cpp`.

The old eight-slot ring freed a live snapshot under rapid updates. On Mac arm64, ASan
reported heap-use-after-free when a reader kept a snapshot through 16 publications. The
regressions in `core/tests/test_threads.cpp` retain a snapshot through 64 updates, check coherent
data, monotonic generations and the final publication count with two writers, and render
two independent instances while each receives alternating LFO and slot curves. The last
test checks both the live LFO source and the modulated cutoff against the same snapshot.
Restoring a process-static generation cache in the instrumented binary caused a TSan
data race between the instances' audio callbacks.
