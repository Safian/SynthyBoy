# Voice screen review against public Serum / Vital documentation

Reviewed 2026-11-24, source inspected on develop (HEAD at final inspection `d57359ad`; other sessions are active). Read-only review: no production/source changes, no builds, no hardware run. Own engine/source only; no Serum/Vital implementation or importer source used.

## Implementation follow-up (same date)

The original audit below describes base4803867b and its limits. The approved Voice implementation now fixes F1 and F2 (`3c878dc4`), with actual audio regressions:36shared/local/mono/RND/HOST rate equivalence comparisons acrossHz/BPM/KEY and Percent/Rate; Noise sample tuning withKEYTRACK on/off, targetOFF, zero-depth,10× random-depth equivalence and audible seeded variation. All-targetsOFF cutoff/ENV tests document the intentionally shared architecture.

F3 now uses eight own native bipolar knobs per selected row: PAN reuses Pan; DETUNE/CUTOFF/ENV/MOD1/MOD2 reuse blue Level. Forty-eight small parameter references share only two definitions. Production90×90px apertures pass input to the host; the overlay retains step numbers, formatted values and sounding-voice dots. Legacy bar painting and the obsolete native bar component/tooling/assets/catalog/tests were removed. The C++ VoiceGrid model/format/live-state adapter remains. Relative fallback drags stay with their starting step and store no parameter truth.

F4 is resolved conservatively by a visible PAN/DETUNE scope label and documented mask semantics. F5 uses existing ownPan artwork with unipolar randomdepth, blueLevel, ActiveText targets/10×, sharedSelector/NameValue and mode-dependent LFO TIME/RATE captions. Geometry comes from oneJSON contract with generated C++ coordinates. Current offline TUI+C++ pictures and evidence limits: [Voice screen](../ui-components/voice-screen.md).

The original external-parity and remaining test-coverage caveats still apply. MPC hardware touch/drawers/performance were not verified by these offline checks.

## Result

The main Voice Control design is recognizably the Serum 2 model, and several core paths have existing audio/UI regression coverage. It is **not correct to declare the whole screen verified**. Two concrete engine omissions and one incomplete native-grid integration were found. Oscillator-target semantics are also narrower than the UI wording suggests. Exact Serum/Vital behavior parity is not established by their documentation.

## Public documentation and authority

1. [Serum 2 official User Guide — Exploring Global Settings, pp.316–318](https://xferrecords.com/manual/serum-2/docs) (also [official entry](https://xferrecords.com/manual/serum-2)). Official search-index excerpts were retrieved during this review. Direct opening redirects to a temporarily signed PDF URL that this web tool could not fetch. The excerpts explicitly show oscillator selection S/A/B/C/N, a sequence length1–8 and PAN/DETUNE/CUTOFF/ENVS/MOD1/MOD2 rows. They describe per-voice random pan and cutoff percentages, pitch offset in cents, envelope variation, and global envelope/LFO scaling with percentage/rate choice. One short exact quote: “You can set the scaling for all envelopes and LFOs.” They do **not** specify exact nonlinear conversion, random seed, note-steal/legato advancement, reset timing or a10× detune switch in the retrieved sections. Those details cannot be certified as Serum parity from this source.
2. [Vital official product documentation](https://vital.audio/): documents drag-and-drop modulation, envelopes, LFO/random sources, stereo modulation and keytracked LFOs. No official direct equivalent of the complete eight-step Voice Control panel was verified. Do not claim our page is a Vital clone or that Vital has a matching10× switch.
3. [Vital official-hosted forum — Every note random pan?](https://forum.vital.audio/t/every-note-random-pan/6433), especially posts2–3: **community advice, not an official manual/specification**. Describes bipolar modulation of oscillator pan from a per-note Random source or S&H/Freeze. This is a closest workflow equivalent, not an exact matching screen.
4. [Vital official-hosted forum — Is there a Vital manual?!](https://forum.vital.audio/t/is-there-a-vital-manual/7829): historical community statements say their linked manual is unofficial. No community manual was silently promoted to official authority here.

## Original prioritized findings (base4803867b)

### F1 — P1: Voice LFO scaling is bypassed by shared free-run / mono clocks

**Source-level confirmed omission; no new audio reproduction run.**

`core/engine-own/voice/voice.cpp:705–707` derives LFO rate factor: percentage mode uses `1/(percent/100)`; Rate mode uses `percent/100`. Local/poly LFO and RND rate paths use that factor (`:408`, `:439`, `:535`, `:963`, `:985`, `:998`).

However `core/engine-own/own_engine.cpp:1212–1226` advances the shared LFO phase, mono LFO core and global RND value from bare `lfoRateHz(...)`, without either Voice scale parameter. `voice.cpp:448–480` consumes the unscaled `g.lfoPhase` / `g.monoCore`; `:994–995` (and reference `:958–959`) consumes unscaled `g.rlfoValue` in Free-run mode. Changing Voice LFO TIME therefore does not affect:

- normal non-KEY Free-run LFO with HOST off;
- the shared mono Envelope/Sustain/Loop modes;
- Free-run random LFO.

HOST-locked Free-run and Free-run KEY use different local paths and may respond, making this mode-dependent rather than a uniformly disabled knob.

**Minimal reproduction to add:** route LFO1 to oscillator gain, use Free-run, HOST off and Hz mode; compare Voice LFO scale100% vs200% in both Percent/Rate modes, then compare Retrigger and mono/Free-runRND. Check actual cycle count/output source progression, not only parameter values. Apply global factor once at the owning shared clock, preserve global synchronization and avoid double-scaling local/host paths. No dedicated existing Voice-scale/free-run regression found.

### F2 — P1: Noise target never receives Voice detune

**Source-level confirmed omission; no new audio reproduction run.**

`voice.cpp:1265` adds masked Voice detune to tracked A/B/C pitch; `:1409` adds masked detune to Sub. Noise mask is correctly index3 (`voice/param_index.cpp:111–113`), and Noise pan applies it at `voice.cpp:1442`.

Both Noise pitch paths omit Voice detune completely:

- `voice.cpp:1446`: generated-noise `noisePitchSemis` is pitch + fine + optional keyboard offset;
- `voice.cpp:1468`: sampled-noise `setSemitoneOffset(...)` uses the same three terms.

Thus enabling NOISE in Voice TARGETS affects PAN but step DETUNE / RANDOM DETUNE /10× have no pitch effect on it, even for a pitched sample with key tracking enabled. This is inconsistent with the target's generic Applies-to-Noise contract, though the public Serum docs alone do not prove every noise-mode detail.

**Regression to add:** isolated pitched Noise sample, nonzero step detune or fixed seeded random detune; compare targetON/OFF and10×. Also exercise generated tonal-noise modes if pitch applies. Keep keytracking-disabled semantics explicit and test them rather than assume.

### F3 — P2:48 native grid sliders are emitted, but production omits their native input apertures

`components/native/voice_grid/voice_grid.py:197–225` builds48 native sliders; `scripts/native_voice/voice_tab.py:343–345` installs them. `shell-mpc/ui/voice_screen.h:139–146` has a `setNativeGridBars()` switch defaulting false, and the only caller found in the entire `shell-mpc` source tree is `ui/tests/ui_voice_grid_host.cpp:145`.

Production `VoiceUi` only enables native lower controls and native sequence/header controls (`overlay/voice_ui.h:74`, `:102–115`). `VoiceScreen::isNativeControl()` at `voice_screen.h:173–185` has no grid-bar aperture. The overlay therefore continues capturing grid touch while native bars exist below it. The original `setNativeSequenceControls()` does propagate `hostDrawsBars` and suppresses overlay bar/handle drawing; the missing production integration is touch aperture ownership and the separate `nativeGridBars_` flag, not proof of unconditional duplicate bar drawing.

This is an incomplete/duplicate migration and invalidates claims that the shipping visible grid has been verified as host-native. The overlay grid may still edit the correct engine parameters; do **not** misreport this as proof of wrong audio. Choose a single owning grid surface, ensure actual display/touch pass-through if native is desired, and test production VoiceUi rather than just toggling a component in isolation. Existing native-grid Python tests prove JSON construction, not that hardware shows or receives those sliders.

### F4 — P2: target-mask scope needs an explicit product contract

Mask order is correctly `{A,B,C,Noise,Sub}` in engine indices; the visual Sub-before-Noise layout is not swapped. But `vc.mask[]` is read only in the PAN/DETUNE application paths. CUTOFF (`voice.cpp:1490`) goes to both per-voice filters regardless of selected oscillator, and random/step ENVS feed a shared envelope factor (`:698`,`:802–806`) regardless of TARGETS. MOD1/MOD2 are general matrix sources and likewise independent of oscillator toggles.

Global ENV/LFO SCALING should be global: that matches its name and the official manual. The open ambiguity concerns **per-voice sequence/random CUTOFF and ENVS** versus a generic oscillator Applies-to label. With every TARGET off, cutoff/envelope sequence/random still changes the voice. Shared filters/envelopes also mean fully isolating one oscillator's cutoff/envelope variation requires more than adding a single mask check.

Do not silently label this full Serum-equivalent target behavior. Decide whether targets intentionally scope PAN/DETUNE only (then document UI accordingly) or whether broader per-source behavior is required; add maskOFF tests for each row. The retrieved manual does not resolve shared-filter/ENV exception details, so this is a contract limitation/open question, not a proven Serum implementation mismatch.

### F5 — P3: waveform-independent knob semantics / captions must survive UI modernization

RANDOM PAN is a unipolar **variation depth**0…100%, not a bipolar fixed L/R pan position (`param_table.cpp:978`, `voice.cpp:693–695`). The existing Pan artwork can be reused only through an appropriate unipolar depth variant; do not map zero toMID or introduce negative depth. The **sequence PAN** row is bipolar−100…100 with MID/L/R formatting (`voice_grid.h:311–331`).

The current native lower dials still use GenericNoLabel instead of our own filmstrips (`scripts/native_voice/voice_tab.py:263–269`); toggle blocks are local Name+Value/ToggleSwitch wrappers (`:308–314`), not the requested ActiveText. Those are already requested UI changes, not new audio defects.

`LFO TIME` / `LFO TIME MODE` captions remain fixed when Rate is selected (`voice_tab.py:95,104`, `voice_screen.h:49,75`). At minimum document inverse time vs direct rate semantics; ideally make the displayed caption unambiguous using existing component APIs.

## Original function map and verdict (base4803867b)

The external documentation establishes the broad function families above. The exact formulas and lifecycle details below are our own current contract, unless stated otherwise; their presence is not proof of bit-identical Serum/Vital behavior.

| Function | Actual production read/write path | Existing coverage inspected | Verdict |
|---|---|---|---|
| Eight-step sequence / LENGTH | `own_engine.cpp:844` increments note sequence on `startNote`; `voice.cpp:690` chooses `(sequence−1)%length`, clamped length1…8. UI-selected step is editor-only. | `test_own_voice.cpp:559` audio two-note pan/detune; `test_own_displays.cpp:755` newest-voice step readout; UI selected-step/value tests. | Works by source/tests for sampled scenarios; exact steal/retrigger/reset parity unspecified externally. |
| Note lifecycle | `Voice::start :248–255` stores sequence and seeds random variation once; `legatoTo :338–355` advances step and invalidates voice/ENV caches but retains random values of the reused voice. All-sounds-off reset clears sequence counter (`own_engine.cpp:581`). Chord notes receive successive sequence numbers; note recall also calls startNote and advances. | `test_own_voice_legato_env.cpp:102,107` guards legato ENV cache; no direct random-on-legato/voice-steal/reset matrix found. | Internally coherent per-voice stable randomness; whether legato should reroll is a design choice, not a proven defect. |
| Sequence PAN | Adds ±1 to selected oscillator PAN then clamps−1…1 (`voice.cpp:695,1271–1273,1414,1442`). | Two-voice audio + A-maskOFF test at `test_own_voice.cpp:559–581`. | Good source path for all5; broader masks/boundaries not directly audio-covered. |
| Sequence DETUNE | ±100cent row becomes±1semitone, A/B/C only if pitch tracking enabled, Sub masked (`:696,1265,1409`). | Same two-note test measures the semitone shift for A. | A/B/C/Sub source path good; Noise omission F2. TrackOFF behavior needs explicit regression. |
| Sequence CUTOFF | ±100% becomes±36semitones then filter settings apply (`:697,1490`). | No dedicated Voice-cutoff response regression found. | Plausible logarithmic own mapping; exact36semitone equivalence unverified; mask scope F4. |
| Sequence ENVS | factor `4^(step/100 + random*depth/100)` then multiplied by globalENV scale (`:698,705`). Delay/attack/hold/decay/release scaled once (`:802–806`); sustain and curves unchanged. | ENV attack scaling test + legato-cache regression. | Sound own mapping; no official exactpow4 requirement; release/all6ENV/beats paths lack dedicated combined regression. |
| Sequence MOD1/MOD2 | row−100…100 maps to source0…1 via`.5+.5*value` (`:699–700,828–829,1031–1032`). Routes are matrix choices, not automatic effect on a fixed target. | `test_own_voice.cpp:614–622` MOD1→OSC gain audio; no equivalent dedicated MOD2 test found. | Correct source publication by inspection; default0 row gives source.5, expected bipolar matrix interpretation must be preserved. |
| RANDOM PAN/DETUNE/CUTOFF/ENVS | Four independently seeded bipolar values stored at fresh voice start (`:250–255`), not re-randomized every block. Depth changes recompute through base-epoch cache (`:768–773`). | Seeded randomPAN deterministic/audio difference test `:598–608`; no direct independent-four-lane/random bounds test found. | Broad per-voice behavior consistent; masks as F2/F4, exact distribution/seed external parity unverified. |
|10× DETUNE | Bool multiplies random detune depth by10 only (`:694`), not sequence detune. Max100cents becomes±1000cents, plus sequence±100cents. At randomDEPTH0, switch has no effect. | Parameter/preset roundtrip `test_user_content.cpp:507,526`; no direct10× pitch-audio regression found. | Arithmetic correct; exact external10× counterpart not documented in retrieved official sections. |
| Global ENV TIME |25…400% factor multiplies all6envelope times through shared settings; source scaling not duplicated in final envelope settings cache. |100ms attack vs400% audio test; legato cache tests. | Good tested subset; missing boundary/release/beats/full-envelope matrix. |
| Global LFO TIME / RATE | Percent means inverse frequency, Rate direct frequency (`:705–707`). Local LFOs and localRND use it. | No dedicated Voice LFO scaling test found. | F1 shared-clock omission; change caption whenRate considered. |
| TARGETS A/B/C/Sub/Noise | UI binds explicit param names, engine order correct; lower host widgets have parameter-backed touch apertures. | A-maskOFF audio; mapping/param coverage UI tests. | PAN across5andDETUNE across4 only; F2/F4. |
| Preset recall |62Voice keys serialized/applied as a unit, omittedVoice values reset defaults; unrelated params untouched, ranges clamped. | `test_user_content.cpp:500–541` covers length/detune/MOD2/10×/LFOscale/SubOFF roundtrip, malformed and clamp cases. | Good targeted preset-source coverage; no new runtime render of recall performed. |
| Touch, header and Q-Link | `voice_ui.h:465–488` selected row/step proxies re-resolve target, queued old-cell writes lose to selection; preset generation checked. `:498–531` publishes4groups×4; NOISE touch-only by deliberate policy. `leftTab :228` disarms native sequence state. | `mastervoice_ui_host.cpp:testVoiceNativeSequence`, `testVoiceNativeControls`, grid/Q/geometry/coverage tests. | Good selection-safety design/covered paths; production nativegridF3 remains. Not hardware-verified in this review. |

## Checks actually executed in this review

- `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/tests -p test_ui_screen_voice.py`: **7tests PASS**,0.035s.
- `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/tests -p test_native_voice_grid.py`: **13tests PASS**,0.012s.
- Existing core/C++ tests were inspected, **not recompiled or rerun**. Passing Python construction tests are not claimed as proof of audio paths or hardware layering. No full suite/baseline repeated, no performance claim, no device measurement.

## Original follow-up inventory; current remaining limits

Items1–5 are addressed by the implementation follow-up above. Items6–7 describe remaining coverage/parity limitations.

1. F1 sharedLFO/RND scaling fix + mode-specific cycle-count/audio regression.
2. F2 Noise detune application + targetON/OFF sampled-noise regression.
3. F3 resolve native-grid ownership/render/touch apertures and render actual TUI+C++ composite.
4. F4 decide and document exact mask scope for CUTOFF/ENVS; add allOFF row tests.
5. F5 preserve unipolar randomPAN depth while using approvedPan art; existing ActiveText migration and10× UI requirements; clarify LFO rate caption.
6. Coverage gaps from table:10× audio ratio, randomfour-lane independence/bounds/legato/steal lifecycle, all5masks, release/allENV/beats scaling, MOD2 routing, globalLFO mode matrix. Add targeted tests with fixes, not a blanket baseline rerun.
7. Exact Serum reset/seed/step/order/nonlinear/10× parity remains unverified; Vital official direct equivalent/manual not verified. Do not turn absence of documentation into a defect.

Potential ownership overlap: F1 touches own_engine.cpp/voice.cpp engine only; F2 voice.cpp engine only. F3/F5 touch Voice skin/grid/UI files, separate from active ENV/LFO-screen/FX/Macro ownership. GlobalLFO fix must coordinate with any engine/LFO worker, not standalone LFO UI worker alone. No files changed by this review except this /tmp report.
