# Voice-count gain: specification (2026-11-19)

The owner's decision: the engine scales its voices by a gain that falls with the number of sounding voices, `G = n_eff ^ -alpha`, alpha = 0.5, so that a chord is about as loud as a single note and its peak lies 3 to 4 dB above the note (the stock MPC instruments: 2 to 8 dB; ours today: 9 to 10 dB, `docs/release/LEVELLING-STOCK-2026-11-19.md` "Chord against note"). This document is the brief of the implementation; the acceptance tests are `core/tests/test_own_voice_count_gain.cpp` (written first: they FAIL until the implementation exists). The per-preset limiters stay out.

## 1. Where the gain applies

- **Own engine only** (`core/engine-own/own_engine.cpp`), in `OwnEngine::process`, in the per control period loop, right after the voices have rendered and the ghosts were added:
  `forEachActive (... render (mo, n, controlPos)); addGhosts (n);` (about line 1568) and **before** anything reads `mixL` / `mixR` (the strip meters of buses 1 and 2, the delay lines `dlVoices`, `dlDirect`, `dlBusIn`, the bus racks, the effects, the master stage).
- It multiplies all four voice targets (`mixL[t]`, `mixR[t]`, t = 0 Main, 1 Bus 1, 2 Bus 2, 3 Direct) for the n samples of the period by the same ramp `g[i] = lerp (gPrev, gCur, (controlPos + i + 1) / kControlPeriod)` (the form of `busGain`). That is the same as a gain per voice (every voice is scaled alike), before the effects and before the master stage and its knee; it costs one ramp and one multiply per sample per used target instead of one per voice.
- When `gPrev == 1.0f && gCur == 1.0f` nothing is multiplied (the loop is skipped): this is what makes alpha 0 and every single-voice passage bit-identical (section 6).
- The source meters of the strips (`stripSq`, measured inside the voices) are not scaled; the bus strips measured after this point are. The voices' own state is untouched; unison is untouched (it already normalises its voices to unit power, `osc/wt_osc.h`, 1/sqrt of the weights).
- Engine-vital: not changed (the shells ship the own engine on the MPC; the desktop default engine is noted in section 9).

## 2. The count n_eff

Once per control period (32 samples, `kControlPeriod`), in the control tick (where `activeCount` and the voice ticks are, around lines 1176 and 1648), over the active voices that are **not** in their 50 ms kill fade (`killing()`), the ghosts excluded:

- `a_i` = the voice's level for the count = `ampCur * velGain` of the voice (envelope 1 times the velocity factor, the values of the voice's last `tick`; add a const accessor `Voice::countLevel()` returning their product; both are already computed per control period). Held and releasing voices both count, by their level: a release tail counts less and less as it decays, a voice that has ended (inactive) not at all.
- `S1 = sum a_i`, `S2 = sum a_i^2`. `n_eff = S1 * S1 / S2` (the participation ratio: n for n equal voices, 1 for one voice, between them for unequal ones). If `S2 < 1e-12f` (silence) or at most one voice counts, `n_eff = 1` exactly.
- A note that starts this period counts with the level of its envelope's first value (`envStart` for an instant attack; for a slow attack it grows with its attack, so the gain follows the attack).
- Mono and legato presets (`master.polyphony` 1 or legato): only one voice is ever counted (the retriggered voice; a stolen voice in its kill fade is excluded), so `n_eff = 1` and the output is bit-identical (section 6).

## 3. The law and the smoothing

- Target: `T = n_eff ^ -alpha` (one `std::pow` per control period, or `std::exp2 (-alpha * std::log2 (n_eff))`: allowed once per period, never per sample). `n_eff == 1` gives `T = 1.0f` exactly (write it explicitly).
- The smoothed gain `gCur` moves towards T once per period, in dB, with a slope limit (the dB of T and of gCur: two `log2` per period at most; or keep the state in dB and convert with one `exp2` per period):
  - **Down** (more voices): if no voice of the period was already sounding at the start of the period (every counted voice started in this period: a chord struck from silence), `gCur = T` at once: nothing held can step, and the chord's first crests are already scaled. Otherwise at most **3.0 dB per millisecond** (at 48 kHz 2.0 dB per control period): a held note steps down by 6 dB in 2 ms when three notes are added, under the attack of the new notes.
  - **Up** (fewer voices, a tail fading, a note ending): at most **0.15 dB per millisecond** (0.1 dB per control period at 48 kHz; 6 dB in 40 ms): no audible swell or click on the notes that remain.
  - Snap: when `|gCur - T| < 1e-6f`, `gCur = T` (so `gCur` returns exactly to 1.0f, which re-enables the skip of section 1).
  - Within the period the per sample linear ramp from `gPrev` to `gCur` (section 1): no zipper.
- `reset ()`, `silence ()`, `prepare ()`: `gPrev = gCur = 1.0f`.

## 4. alpha: constant, switch, defaults

- `constexpr float kVoiceCountAlphaDefault = 0.5f;` in `core/include/synthyboy/engine.h` next to `setOwnSavingsOff`, and an engine switch for tools and tests (no host parameter: the host's parameter count, 2719 in the overlay build, does not change; not in the chunk, not in a preset):
  `bool setOwnVoiceCountAlpha (IEngine& engine, float alpha) noexcept;` (false if not an own engine; alpha clamped to 0 .. 1; any thread, an atomic read at the start of every block, like the savings) and `float ownVoiceCountGain (const IEngine& engine) noexcept;` (the `gCur` of the last control period; 1 for another engine; for the tests and a meter). Define `SYNTHYBOY_HAS_VOICE_COUNT_GAIN 1` in `engine.h` with them (the acceptance tests compile against it).
- The shipped engine uses `kVoiceCountAlphaDefault` = 0.5 (1 / sqrt(n): a chord of equal voices is as loud, in power, as one voice; the paper prototype gave a chord-to-note peak of 2.9 to 4.4 dB, the stock 2 to 8 dB, and 9 presets held back by the -1 dBFS rule instead of 65). No reason was seen for another value; 0.25 is the fall-back if the owner hears the chords as too quiet (chord-to-note peak 6 to 7.4 dB).
- Tools: `synthyboy_preset_render` and `synthyboy_level_probe` get `--voice-gain-alpha A` (calls `setOwnVoiceCountAlpha` on every engine they make; default: the engine's default, 0.5). `bench/tier_bench` may get the same switch for CPU comparisons.
- After the implementation the presets must be levelled again (`scripts/level-to-stock.py solve --gain 0 --gate ...`, the probe with the engine default), because the chords and the held-back list change.

## 5. Real-time and NEON

- No allocation, no lock, no libm call per sample: the count and the target are made once per control period; the per sample work is the ramp and a multiply. The existing `process_does_not_allocate` test (`core/tests/test_rt_alloc.cpp`) covers it with the default alpha.
- ARM: build the ramp once per period into a `float g[kControlPeriod]` (scalar, the exact `lerp` form) and multiply the used targets with `vmulq_f32` four samples at a time, the remainder scalar; the samples equal the scalar loop bit for bit (one IEEE multiply each; keep `-ffp-contract=off` semantics: no fused multiply-add between the ramp and the product). No float compare in NEON is needed (the skip test is scalar, once per period). arm32 and AArch64 alike.

## 6. Bit identity (the regression gate)

- alpha = 0: `T = 1` always, `gCur` stays 1.0f, the multiply is skipped: every sample identical to the engine before the change (all presets, all tiers).
- alpha = 0.5 and at most one counted voice at every period (one note at a time, its own release tail included, mono / legato presets): identical to alpha 0.
- Anything else differs by design.

## 7. What the owner will hear (alpha 0.5, voices of equal level)

| voices sounding | gain of each voice | a chord of them against one note |
|---|---|---|
| 1 | 0 dB | - |
| 2 | -3.0 dB | power equal to one note, peak up to +3 dB |
| 4 | -6.0 dB | power equal to one note, peak up to +6 dB (measured median chord-minus-note peak about 3 to 4 dB) |
| 8 | -9.0 dB | power equal to one note |

- A chord struck at once: scaled from its first sample.
- A note held while three more are added: it gets quieter by 6 dB within about 2 ms, under the attack of the new notes (listen for a dip of the held note).
- Notes released while one is held: the held note comes back up by at most 0.15 dB per millisecond as the tails fade (listen for a swell: 6 dB in at least 40 ms).
- Pads with long releases: playing a new chord over the tails of the last one counts the tails by their level, so the new chord starts a little quieter than one struck in silence.

## 8. Acceptance tests (`core/tests/test_own_voice_count_gain.cpp`, target `synthyboy_test_own_voice_count_gain`)

All need `SYNTHYBOY_HAS_VOICE_COUNT_GAIN`; without it each test FAILs with "voice-count gain not implemented". The test patch: the defaults with Osc 1 a sine (`osc1.wavetable` 0), Filter 1 off, Env 1 attack 0 ms, sustain 100 %, release 50 ms, velocity tracking 0 %, polyphony 16, no effects; 48 kHz, 128 frame blocks unless stated.

1. `voice_count_gain_alpha_zero_and_single_voices_are_bit_identical`: every factory preset (Key 61 and XL profiles), a single note 48 at velocity 1.0 held 1 s and released for 1 s, at alpha 0.5 and at alpha 0: every sample equal (bit pattern) for the monophonic presets (`master.polyphony` 1); a polyphonic preset whose single note sounds with several voices (an arpeggio, a strum, retriggers) may differ by design and is listed. The test patch: one note at alpha 0.5 equals alpha 0; a chord of 4 notes at alpha 0 in two engines: equal, and the gain reads 1.0.
2. `voice_count_gain_steady_state_follows_n_to_the_minus_alpha`: the test patch, n = 2, 4, 8 notes struck together; the ratio of the output at alpha 0.5 to the output at alpha 0 (the same render otherwise), RMS over 0.2 .. 0.5 s: -3.01, -6.02, -9.03 dB within 0.05 dB; `ownVoiceCountGain` at 0.3 s equals 2^-0.5, 4^-0.5, 8^-0.5 within 1e-4.
3. `voice_count_gain_slopes_are_bounded`: the test patch in 32 frame blocks, one note held from 0, three more added at 0.3 s, all four released at 0.8 s except the first, held to 1.6 s: the change of `ownVoiceCountGain` between two control periods at most 2.0 dB going down and 0.1 dB going up (48 kHz); 4 notes struck from silence: the gain of the first period after the note-on within 0.05 dB of -6.02 dB (the instant rule); after the last release the gain is back to exactly 1.0f within 0.2 s of the tails' end.
4. `voice_count_gain_closes_the_chord_note_gap`: every factory preset (Key 61 profile): the raw peak of the stock-voiced chord 48 55 60 64 minus that of the note 48, velocity 1.0, held 1 s, at alpha 0 and at alpha 0.5; prints the table per category (median gap at 0 and at 0.5) and asserts the median gap over all presets falls by 4.5 to 7.0 dB.
5. No allocation: the existing `process_does_not_allocate` (`core/tests/test_rt_alloc.cpp`) with the default alpha 0.5 (nothing to add).

The bit identity against the engine BEFORE the change (alpha 0) is a two-build comparison (section 9, steps 4 and 5).

## 9. What the runner executes (once, after the implementation is merged)

1. Configure (own engine; one build dir): `cmake -S . -B build-vcg -G Ninja -DCMAKE_BUILD_TYPE=Release -DSYNTHYBOY_WITH_VITAL=OFF -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BUILD_TESTS=ON`
2. Build the targets: `bash .hw-local/tools/buildslot.sh cmake --build build-vcg -j4 --target synthyboy_test_own_voice_count_gain synthyboy_test_rt synthyboy_test_own_voice_opt synthyboy_test_output_stage_presets synthyboy_preset_render synthyboy_level_probe`
3. Tests: `build-vcg/core/tests/synthyboy_test_own_voice_count_gain`, `build-vcg/core/tests/synthyboy_test_rt`, `build-vcg/core/tests/synthyboy_test_own_voice_opt`.
4. Reference renders from the commit before the implementation (a second build dir `build-vcg-base` at that commit, same configure, target `synthyboy_preset_render`): `build-vcg-base/tools/preset-render/synthyboy_preset_render --presets content/presets --engine own --tier all --content-dir build-vcg-base/tools/preset-render/content-store --wav R0 --chord-wav R0c --float32 --hash`
5. The new build at alpha 0: the same command with `build-vcg/...` and `--voice-gain-alpha 0` into `R1`, `R1c`; `bash scripts/compare-renders.sh R0 R1` and `bash scripts/compare-renders.sh R0c R1c`: exit 0 (every file identical).
6. The levelling again (section 4) and `synthyboy_test_output_stage_presets` with the new held-back list; the preset gate (`synthyboy_preset_render --engine own --summary-only`).
7. On the devices: the CPU of the heaviest presets (the extra cost is one ramp and multiply per sample per used target and two log / exp per control period) and a listening check of section 7.

The desktop shells: the JUCE shell uses the engine of `SYNTHYBOY_SHELL_ENGINE` (Vital by default where it is built): the voice-count gain reaches the desktop only with the own engine; the levelling and the MPC use the own engine.
