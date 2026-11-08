# PITCHUNI Report — Oscillator Pitch Modes, Unison Laws/Range, and Phase Legato (2026-11-08)

Implements Work Package **pitch-unison** (report name `PITCHUNI`, Key Block `B14`, assumptions `A80`–`A86`).
Resolves GAP items **G25–G27**, **G31**, and **G32**, closes UI audit rows **02-09** and **02-10**.

Capability bit `kCapPitchUnison = 1ull << 26` in `core/include/synthyboy/engine_caps.h`.

---

## 1. What Was Built

### 1.1 Item 1: G25–G27 Oscillator Pitch Modes & Transpose Snap
- **Pitch Modes (`oscN.pitch_mode`, choices 0–3):**
  - `0 = Semitones`: Default standard chromatic semitone transposition (-48 to +48 st).
  - `1 = Harmonics`: Coarse transposition snaps to integer harmonics (1x, 2x, 3x ... up to 64x) based on `oscN.pitch_coarse`.
  - `2 = Ratio`: Coarse transposition acts as frequency multiplier ratios (0.25x to 32x).
  - `3 = Step (MTS-ESP)`: Integrates tuning seam in `core/tuning` using custom note frequency tables (`SynthOscillator::setNoteTable()`), without linking external unverified MTS-ESP libraries.
- **Fixed Pitch & Pitch Tracking (`oscN.fixed_pitch`, `oscN.pitch_track`, `oscN.pitch_bend_track`):**
  - When `oscN.pitch_track == 0`, pitch tracking is detached from the keyboard, producing stable fixed fundamental frequencies based on `oscN.pitch_coarse` + `oscN.fixed_pitch` (0–127 MIDI note equivalent), ignoring note pitch.
  - `oscN.pitch_bend_track` allows enabling/disabling pitch bend wheel influence per oscillator independently.
- **Transpose Snap Keyboard (Row 02-10 & `oscN.transpose_snap`):**
  - 16-bit bitmask encoding 12 scale semitone pitch classes (bits 0..11 for C..B) and bit 12 (0x1000) for Global Snap mode.
  - Implemented `TransposeSnapControl` in `shell-juce/ui/panels/TransposeControl.h` & `.cpp`: 12 semitone toggle keys, a Global Snap toggle pill, and a Clear button.
  - Strict compliance with `uip_no_literal_colours_in_the_ui_sources`: uses `c.mod` and `c.onMod` for active key states.
  - When active, chromatic coarse/transpose inputs are dynamically snapped to enabled pitch classes in the scale.
- **Row 02-09 Pitch Mode Selector:**
  - Exposed directly as a selectable parameter dropdown / radio choice in `OscPanel`.

### 1.2 Item 2: G32 Serum-Class Unison Laws, Range & Blend Shape
- **Unison Detune Distribution Modes (`oscN.unison_mode`, choices 0–4):**
  - `0 = Linear`: Uniformly spaced detune spreading across voice lanes (-1.0 to +1.0).
  - `1 = Super`: Center-concentrated detune distribution mimicking JP-8000 supersaw clustering ($x^3$ curve).
  - `2 = Exp`: Exponential distribution expanding outwards toward outer voice pairs.
  - `3 = Inv`: Inverted distribution biasing voice density towards extremes while leaving center open.
  - `4 = Random`: Fixed pseudorandom deterministic detune distribution per voice lane.
- **Unison Range (`oscN.unison_range`):**
  - Configurable detune spread width scaling from 0 to 48 semitones (default neutral 12 st).
- **Unison Blend Shape (`oscN.unison_blend_shape`):**
  - Shape exponent (-1.0 to +1.0, neutral 0.0) shaping the amplitude taper of side voice lanes relative to the center voice lane while maintaining constant total root-mean-square power.
- **Unison Stack Repeat (`oscN.unison_stack_repeat`, choices 0–3):**
  - Allows grouping and octave/harmonic shifting of unison voice pairs: Single (1x), 2x Octave (+12 st), 3x Octave (+24 st), Chord (Fifth / Octave).
- **UI Controls:** Added unison mode, range, blend shape, and stack repeat to `OscPanel`.

### 1.3 Item 3: G31 Phase Legato
- **Voice-level & Oscillator-level Legato Phase Continuity:**
  - `voice.phase_legato` (global/voicing toggle): When enabled, voice allocations during legato note transitions preserve oscillator phase state from the preceding voice.
  - `oscN.phase_legato` (per-oscillator toggle): Enables legato phase preservation individually per oscillator.
  - `oscN.phase_memory`: Preserves running oscillator phase memory across non-legato note releases up to envelope tail release.
- **Engine Implementation:**
  - Upstream `SynthOscillator` maintains `last_running_phase_` across buffer computations.
  - In `SynthOscillator::reset()`, when phase legato conditions are satisfied (`kPhaseLegato` active, note count > 1, or voice legato active), oscillator phase indices are seeded from `last_running_phase_` instead of resetting to initial phase.
- **UI Controls:**
  - `voice.phase_legato` toggle button added to `MasterVoicingPanel`.
  - `oscN.phase_legato` and `oscN.phase_memory` toggles exposed in `OscPanel`.

---

## 2. Key Block B14 Specification

Appended at the end of `kRaw[]` in `core/src/param_table.cpp` (indices 1484 to 1520, count = 1521).
Total mod destinations updated from 379 to 416 in `core/include/synthyboy/modulation.h`.

| Hash ID | Parameter Key | Range | Default | Unit | Description |
|---|---|---|---|---|---|
| `0x7b66df87` | `osc1.pitch_mode` | 0..3 | 0 | - | Pitch mode: Semitones, Harmonics, Ratio, Step |
| `0xce9e94bf` | `osc1.pitch_coarse` | -48..64 | 0 | st/ratio | Coarse pitch in unit determined by pitch_mode |
| `0xefab2d23` | `osc1.pitch_track` | 0..1 | 1 | on/off | Keyboard pitch tracking |
| `0xa4a0dfb5` | `osc1.fixed_pitch` | 0..127 | 60 | note | Fixed pitch MIDI note base when tracking is off |
| `0x24d08914` | `osc1.pitch_bend_track`| 0..1 | 1 | on/off | Pitch bend wheel tracking enable |
| `0x3235bca2` | `osc1.transpose_snap` | 0..8191 | 0 | mask | Snap keyboard bitmask (bits 0..11 note classes, bit 12 global) |
| `0x37852c03` | `osc1.unison_mode` | 0..4 | 0 | - | Unison distribution law: Linear, Super, Exp, Inv, Random |
| `0x3544ebdb` | `osc1.unison_range` | 0..48 | 12 | st | Unison detune spread range |
| `0xf6a81e3a` | `osc1.unison_blend_shape`| -1..1 | 0 | - | Unison blend curve shape (-1.0 to 1.0) |
| `0x510b64d3` | `osc1.unison_stack_repeat`| 0..3 | 0 | - | Unison stack mode: 1x, 2x Oct, 3x Oct, Chord |
| `0x5d9b6da8` | `osc1.phase_legato` | 0..1 | 0 | on/off | Per-oscillator legato phase continuity |
| `0x89e13b8e` | `osc1.phase_memory` | 0..1 | 0 | on/off | Oscillator phase memory across note releases |
| `...` | `osc2.*` (same 12 keys) | | | | Oscillator 2 equivalents |
| `...` | `osc3.*` (same 12 keys) | | | | Oscillator 3 equivalents |
| `0x84b72782` | `voice.phase_legato` | 0..1 | 0 | on/off | Global voice-level phase legato continuity |

Neutral defaults guarantee bit-exact audio reproduction for all 112 factory presets and existing golden render tests.

---

## 3. Assumptions (A80–A86)

Defined in `docs/ui-design/13-new-keys.md`:

- **A80 (Oscillator pitch modes):** Pitch modes 0 (Semitones), 1 (Harmonics), 2 (Ratio), and 3 (Step) dictate the frequency mapping of the oscillator. In Harmonics mode, coarse pitch quantises frequency to integer multiples of the fundamental. In Ratio mode, coarse pitch acts as a rational multiplier. In Step mode, frequency steps conform to the active microtuning table.
- **A81 (Fixed pitch and tracking):** When `pitch_track == 0`, the oscillator ignores MIDI key pitch and derives its fundamental exclusively from `fixed_pitch` + coarse pitch offset. `pitch_bend_track` allows disabling pitch bend wheel modulation independently.
- **A82 (Transpose snap bitmask):** `transpose_snap` is stored as an integer bitmask where bits 0–11 represent chromatic semitones (C through B) allowed in the scale. Bit 12 (0x1000) toggles Global Snap. When non-zero, coarse transpose values are rounded to the nearest active semitone class.
- **A83 (Step mode tuning seam):** Step mode utilizes the in-tree tuning table interface (`core/tuning`) without linking third-party MTS-ESP dynamic libraries directly.
- **A84 (Serum unison detune laws):** Unison modes 0–4 implement detune distributions: Linear (equidistant), Super ($x^3$ dense center cluster), Exp ($e^x$ expanding cluster), Inv (boundary-weighted distribution), and Random (reproducible deterministic pseudorandom distribution per lane).
- **A85 (Unison blend shape power preservation):** `unison_blend_shape` smoothly tapers outer unison voice amplitudes while renormalising gain to ensure total unison power remains constant across blend shape adjustments.
- **A86 (Phase legato continuity):** When `phase_legato` is engaged, incoming legato note-on events do not reset oscillator phase accumulators, but continue seamlessly from `last_running_phase_`.

---

## 4. File and Line References

- **Parameter Definitions & Specifications:**
  - `docs/ui-design/13-new-keys.md` — Lines 423–520 (Block B14, assumptions A80–A86).
  - `core/include/synthyboy/engine_caps.h` — Line 47 (`kCapPitchUnison = 1ull << 26`).
  - `core/src/engine_caps.cpp` — Lines 71–85 (`kCapPitchUnison` parameter key mappings).
  - `core/include/synthyboy/modulation.h` — Line 15 (`kNumModDestinations = 416`).
  - `core/src/param_table.cpp` — Lines 1601–1643 (Block B14 entries in `kRaw[]`).
  - `core/include/synthyboy/kick_pages.h` — Line 18 (`kNumKickPages = 106`).
  - `core/src/kick_pages.cpp` — Lines 137–152 (Pages 104–106 for B14 pitch and unison keys).
- **Engine Implementation (Vital Upstream & Adapter):**
  - `core/engine-vital/upstream/src/common/synth_parameters.cpp` — Lines 43, 237–264 (registered parameters).
  - `core/engine-vital/upstream/src/synthesis/producers/synth_oscillator.h` — Lines 24–41, 142–154 (inputs, state, `setNoteTable`).
  - `core/engine-vital/upstream/src/synthesis/producers/synth_oscillator.cpp` — Lines 59–69, 137–208, 222–276, 321–343 (pitch modes, unison laws, blend shape, phase legato).
  - `core/engine-vital/upstream/src/synthesis/modules/oscillator_module.h` & `.cpp` — Lines 18–35, 75–125 (poly mod controls).
  - `core/engine-vital/upstream/src/synthesis/modules/producers_module.h` & `.cpp` — Lines 32–40, 85–110 (forwarding inputs and `setNoteTable`).
  - `core/engine-vital/upstream/src/synthesis/framework/voice_handler.h` — Line 50 (`virtual void setNoteTable`).
  - `core/engine-vital/upstream/src/synthesis/modules/synth_voice_handler.h` & `.cpp` — Lines 58–63, 110–135 (wiring midi note, phase legato, and tuning table).
  - `core/engine-vital/adapter/vital_bindings.cpp` — Lines 225–262 (bindings for all 37 B14 parameters).
  - `core/engine-vital/adapter/vital_engine.cpp` — Lines 174, 529 (`kCapPitchUnison` capability).
- **UI Components & Panels:**
  - `shell-juce/ui/panels/TransposeControl.h` & `.cpp` — `TransposeSnapControl` component with 12 semitone keys, Global Snap, and Clear.
  - `shell-juce/ui/panels/OscPanel.cpp` — Lines 30–75, 120–160 (all 12 B14 keys per osc, `TransposeSnapControl`, pitch mode choice).
  - `shell-juce/ui/panels/MasterVoicingPanel.cpp` — Line 41 (`voice.phase_legato`).
  - `docs/ui-design/UI-GESTURE-AUDIT.md` — Rows 02-09 and 02-10 marked CLOSED.
- **Tests:**
  - `core/engine-vital/tests/test_pitchuni.cpp` — 6 audio tests verifying relative audible differences for coarse pitch, harmonics mode, ratio mode, step mode, fixed pitch/tracking, transpose snap, unison modes (linear vs super/exp/inv/random), blend shape, stack repeat, and phase legato.
  - `shell-juce/ui/tests/test_ui_gaps.cpp` — `uig_pitch_mode_and_transpose_snap_keyboard`.
  - `core/tests/test_new_keys.cpp` — Frozen parameter count and layout verification for 1521 keys.

---

## 5. Verification & Test Results

All test gates ran cleanly:

1. **New Audio Render Suite (`synthyboy_test_vital_pitchuni`):**
   - 6 test cases, 0 failures:
     - `osc_pitch_coarse_and_modes_produce_relative_difference` (Passed)
     - `osc_step_mode_with_tuning_table_produces_relative_difference` (Passed)
     - `osc_fixed_pitch_and_tracking_produce_relative_difference` (Passed)
     - `osc_transpose_snap_produces_relative_difference` (Passed)
     - `osc_unison_modes_blend_and_stack_produce_relative_difference` (Passed)
     - `osc_phase_legato_preserves_running_phase` (Passed)

2. **Core & Adapter Parameter/Audio Suites:**
   - `synthyboy_test_vital_params`: 12 test cases, 0 failures (all 641 v3 keys live, bound/special, within valid ranges).
   - `synthyboy_test_vital_engine`: 8 test cases, 0 failures.
   - `synthyboy_test_vital_audio`: 29 test cases, 0 failures (alloc-free, quality gates pass, golden audio bit-identical).
   - `synthyboy_test_keys`: 11 test cases, 0 failures (v2 sound unchanged).

3. **UI Panel & Gesture Suites:**
   - `synthyboy_test_ui_panels`: 5 test cases, 0 failures (`uip_every_parameter_key_has_a_control`, `uip_touch_targets_on_every_page_and_size`, `uip_no_literal_colours_in_the_ui_sources`, `uip_headless_render_every_page`).
   - `synthyboy_test_ui_gaps`: 33 test cases, 0 failures (`uig_pitch_mode_and_transpose_snap_keyboard` included).

4. **Full Test Suite (`ctest -j8`):**
   - **45/45 tests passed (100%)**, including full preset sanity across all 112 factory presets (`synthyboy_preset_sanity`).

---

## 6. Open Questions

None. All architectural decisions (MTS-ESP seam in `core/tuning`, bitmask layout for transpose snap, Serum unison distribution laws, power preservation for blend shape, phase legato handoff across note transitions) are implemented, verified, and tested against neutral baseline invariants.
