# SynthyBoy Sample Oscillator (core/sample-osc) Report

## 1. Overview and Status
- **Namespace**: `synthyboy::sampleosc`
- **Package**: `core/sample-osc`
- **Design alignment**: Implements owner decision Q6 (Sample oscillator), Serum manual sample oscillator chapter (pages 68-84), and `docs/ui-design/02-oscillators.md` (§1.4).
- **Status**: 100% COMPLETE. All required features, data structures, voice rendering DSP, WAV parsing, mipmap generation, interpolation tiers, anti-click handling, and tests are implemented and passing.

## 2. Key Components Delivered
1. **Sample Types & Data Structures (`include/synthyboy/sampleosc/sample_types.h`)**:
   - `SampleView`: Lightweight, non-allocating, non-owning planar float frame view (mono/stereo, any sample rate, arbitrary duration).
   - `SampleOscParams`: Plain real-time C++ parameter struct without dependence on the `ParamInfo` table.
   - `LoopMode`: `none` (one-shot), `forward` (loop with crossfade), `pingPong` (alternating bidirectional), `reverse` (repeated reverse loop).
   - `QualityTier`: `low` (4-point windowed-sinc), `medium` (8-point windowed-sinc), `high` (16-point windowed-sinc with inter-mip blending).
   - `OverviewPoint`: Min/max envelope per pixel column for UI waveform rendering.
   - Denormal and NaN/inf safety helpers (`flushDenormal`, `sanitizeFloat`).

2. **Non-RT Sample Buffer & Mipmap Generator (`include/synthyboy/sampleosc/sample_data.h`, `src/sample_data.cpp`)**:
   - `SampleData`: Owning planar float buffers for audio channels.
   - Automatic octave mipmap pyramid generation (levels 0 to 7) using a 101-tap Kaiser-windowed sinc FIR filter ($\beta = 8.0$) for band-limited pitch-up with > 55 dB stopband alias attenuation.
   - Waveform overview generation: `computeOverview` and `computeOverviewStereo` for $N$ pixels.
   - Move semantics, clear, and real-time safe `view()` generation.

3. **WAV File Reader (`include/synthyboy/sampleosc/wav_reader.h`, `src/wav_reader.cpp`)**:
   - Decodes 16-bit PCM, 24-bit PCM, 32-bit PCM, and 32-bit IEEE float formats.
   - Supports mono and stereo files.
   - Strict sanity limits (max 64M frames, max 512 MB file size, 1000..384000 Hz sample rate).
   - Robust chunk parsing (`fmt `, `data`, `inst`, `smpl`) with full fuzzer/garbage resilience.
   - Metadata extraction: root note and fine-tuning from filename suffixes (preceded by space or underscore, e.g., `"Morning Bass F2.wav"`, `"Piano Eb3.flac"`), `'inst'` chunks, and `'smpl'` sampler loop markers.

4. **Polyphony-Ready Voice DSP (`include/synthyboy/sampleosc/sample_voice.h`, `src/sample_voice.cpp`)**:
   - Real-time safe: Zero heap allocation and no locks in `process()`, `processAdd()`, `noteOn()`, `noteOff()`, or `stop()`.
   - Sample rates supported: 22050 Hz to 192000 Hz; block sizes 1 to 4096 frames (strictly block-size invariant).
   - High-precision windowed-sinc interpolation with 512-phase precomputed polyphase tables (unity DC gain).
   - Band-limited anti-aliasing resampling using octave mipmaps (with continuous inter-mip crossfade in high quality tier).
   - Equal-power loop crossfade ensuring click-free wrap-around across loop boundaries.
   - Ping-pong turnaround symmetry and reverse playback.
   - Smooth anti-click fade-in (~1.5 ms) and fade-out (~2.0 ms) ramps on note start and voice stop.
   - Constant-power stereo panning and linear gain staging.
   - Unison hooks: per-voice pitch ratio multiplier, start phase offset, stereo pan offset, and level multiplier.

5. **Build Integration**:
   - Per-package `core/sample-osc/CMakeLists.txt` creating static library `synthyboy_sampleosc` and test executable `synthyboy_test_sampleosc`.
   - Exactly ONE line added to `core/CMakeLists.txt`: `add_subdirectory(sample-osc)`.
   - Clean compilation under `-Wall -Wextra -Wpedantic` with zero warnings.

## 3. Test Suite (`tests/test_sample_osc.cpp`)
All 12 test cases and 5343 checks pass:
- `sampleosc_wav_filename_note_parsing`: Verifies note suffix parsing (C-1 to G9, sharps, flats, separators).
- `sampleosc_wav_reader_formats`: Verifies 16-bit, 24-bit, 32-bit integer PCM and 32-bit IEEE float decoding, metadata extraction.
- `sampleosc_wav_reader_garbage_inputs`: Rigorous fuzzing with truncated files, null pointers, corrupt RIFF/fmt/data headers, and random byte streams.
- `sampleosc_pitch_accuracy_5_octaves`: High-resolution FFT peak frequency measurement across 5 octaves (A0..A5), verifying accuracy within < 0.5% (approx 8 cents).
- `sampleosc_loop_continuity_no_click`: Verifies step bound across crossfaded loop boundary ($\Delta s < 0.03$ on a discontinuous ramp).
- `sampleosc_ping_pong_symmetry`: Verifies mathematical symmetry around loop turnaround points.
- `sampleosc_reverse_equals_reversed_forward`: Verifies reverse playback equals time-reversed forward playback.
- `sampleosc_aliasing_sanity_noise_pitch_up`: Verifies pitch-up of white noise and pure tone (14 kHz pitched up by 2 octaves) achieves > 55 dB alias attenuation.
- `sampleosc_overview_computation`: Verifies min/max pixel envelope calculations.
- `sampleosc_unison_and_denormal_safety`: Verifies multi-voice unison stacking and NaN/denormal immunity.
- `sampleosc_samplerates_and_blocksizes`: Verifies identical sample-accurate output across block sizes (1 sample vs 64 samples vs 4096 samples) and sample rates (22.05 kHz .. 192 kHz).
- `sampleosc_quality_tiers`: Verifies low, medium, and high interpolation tiers.

## 4. Verification Output
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
CTest summary:
`100% tests passed out of 13` (all previous tests 1-11 + 13 pass unchanged, new test 12 passes).
