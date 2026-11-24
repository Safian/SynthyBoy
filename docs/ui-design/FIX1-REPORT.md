# FIX1 report - determinism, oscN.type crossfade, Bode latency, warnings (2026-11-08)

## 1. state_random_walk_saves_loads_and_renders_the_same (regression)

* Symptom: `synthyboy_test_vital_audio` failed 4 of 5 runs on develop (tier high "DIFFERENT", rms of the original varied 0.0734 .. 0.0745 between runs, tier low always identical): timing dependent, not a state / float problem.
* Cause: the Convolve module rebuilds its impulse on a builder thread (poll 10 ms) and the audio thread took the finished convolver over "whenever it was ready". The random walk over all 1484 keys switches Convolve on with random size / decay / damp, so the block in which the convolver appeared depended on thread timing. Tier low masks Convolve, hence identical there. The same engine rendered twice gave different audio.
* Fix (engine, test untouched): `Quality::realtime` (new, default false). In the default (offline / deterministic) mode the audio block that posts a rebuild, or follows a new impulse or sample rate, waits (yield, no allocation) until the builder has published the build for exactly those versions and takes it over in that very block (documented rule in `serum_fx_modules.cpp` and `core/engine-vital/README.md`). `realtime = true` keeps the old never-wait behaviour. The JUCE shell sets `realtime = !isNonRealtime()` (prepareToPlay and `setNonRealtime`), the CLAP shell sets it true (live).
* Proof: `ctest -R vital_audio` 5 of 5 passed after the fix (4 of 5 failed before). New test `convolve_rebuild_is_deterministic_offline_and_does_not_wait_in_realtime_mode` (two runs with a size change every block and no pauses are bit identical; realtime mode does not block).

## 2. WP4-8 BLOCKED items

* (a) oscN.type crossfade: `OscillatorModule` runs both oscillator types for 10 ms after a type change while the oscillator is sounding and blends them (the sample oscillator now writes into its own outputs, which the module copies or blends; counters per module clone because the voice clones share the processor flags). The adapter keeps the sample view while the type is Wavetable (it used to null it, which killed the sample voice at once). Test `oscillator_type_change_under_a_sounding_note_crossfades_in_10_ms` (both directions: largest sample step around the switch equals the plain sine step). Limit unchanged: a Sample oscillator starts with the next note, so wavetable to Sample under a held note fades the note out.
* (b) Bode latency: `IEngine::latencySamples()` (host rate, as of the last block). The chain computes the latency of every path from the configuration (Bode 64 samples at the chain rate, 32 host samples with oversampling 2) and delays all other paths with `AlignDelay` lines: voices into Main, buses to Main / Direct / the other bus, the Main result, the Direct input, the faster bands of a splitter. Reported by JUCE (`setLatencySamples`) and CLAP (`latency.get`, `request_restart` on a change). Test `bode_reports_its_latency_and_every_other_path_is_delayed_to_match` (Bode in Main, Bus 1 and a splitter band; oversampling 1 and 2: with mix 0 the output is the output without Bode shifted by exactly the reported latency, difference 0). Toggling Bode changes the latency (and clears the delay lines once); the host decides when to apply it.
* (c) `loop_link`: 13-new-keys.md A39 says "editor action without DSP effect" (02-oscillators.md has no DSP for it). Nothing to implement; removed from BLOCKED in WP48-REPORT.md.

## 3. Warnings

* `kNumEnv` (unused) removed from `vital_engine.cpp`.
* `-Wmissing-field-initializers` for `modX`: `ContentPoint` got a constructor (x, y, power) so `{ x, y, power }` is complete; the three explicit `{}, {}` initialisers in `test_wp123.cpp` were shortened. This also silenced the 5 warnings of `test_rt_alloc.cpp`. The build has no compiler warnings (only the linker note about duplicate libraries).

## 4. clap-validator and gates

* `cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=ON && cmake --build build -j8`: exit 0, no compiler warnings. `ctest --test-dir build -j6`: 33 / 33 passed (incl. `synthyboy_test_vital_audio`, `vital_wp4`, `vital_wp7`, `rt` with 0 allocations). `scripts/check-params-doc.sh`: up to date (no key changed).
* clap-validator on `build/SynthyBoy_artefacts/Release/CLAP/SynthyBoy.clap`: 44 tests run, 33 passed, **0 failed**, 1 warning (`state-invalid-random`, pre-existing, not a reproducibility test; `state-reproducibility-basic` passed), 10 skipped.
