# SynthyBoy - MPC Release Change Log (Draft)

SynthyBoy is an 8-voice polyphonic synthesizer plugin built as VST3, CLAP, and (macOS only) Audio Unit from a single codebase (README.md#1-3).
It incorporates two switchable sound engines behind a common interface: a lightweight original engine and an extended Vital-derived DSP engine (README.md#engines).
For standalone Akai MPC hardware (MPC Key 61, MPC XL), it compiles as a headless VST2 module packaged under `Safian - VST - SynthyBoy` running the original engine at quality tier low (docs/MPC.md#build).
Hardware interaction is provided via native MPC skin definition files, hardware Q-Link mapping, and an experimental out-of-process GPU overlay rendering directly to hardware display planes (docs/MPC.md#package-layout, docs/MPC.md#overlay-experimental).
The instrument includes 112 factory presets, 3 wavetable/sample/granular oscillators, 37 filter types, 6 envelopes, 10 LFOs, a 64-slot modulation matrix, and built-in effects (README.md#factory-content, docs/MPC.md#sound-differences-at-tier-low).

---

## State of the MPC Build

### Sound Engine
- **Engine codebase**: The MPC build runs SynthyBoy's own engine (`core/engine-own`), an original C++17 DSP codebase completely free of JUCE and isolated from GPL Vital source code (docs/ARCHITECTURE.md#2.2, core/engine-own/README.md).
- **Execution tier**: The engine runs strictly at `Tier::low`, enforcing an 8-voice polyphony limit and a unison cap of at most 3 unison voices per oscillator (`core/include/synthyboy/quality.h`, docs/MPC.md#sound-differences-at-tier-low, c3a2065, 91f2d75).
- **Oscillator models**: Features 3 main oscillators supporting Wavetable (with spectral morph and two warp slots), Sample, Granular, Spectral, and Multisample modes, plus a dedicated Sub oscillator (6 shapes) and Noise source (9 noise types / sample) (docs/MPC.md#oscillator-ui, docs/ARCHITECTURE.md#3.2, d90e1a0a, 42682c52).
- **Filter collection**: Includes 2 main filters and an effect filter covering 37 models (Ladder, Diode, Analog, Dirty, Digital, Formant, Comb, Scream, etc.) with serial, parallel, and blend routing (docs/MPC.md#filter-ui, docs/ARCHITECTURE.md#3.4, 69460e77, a1166d2c).
- **Modulator complement**: Provides 6 envelopes with adjustable curvature parameters, 10 LFOs with custom shape support, and 4 random LFOs (docs/MPC.md#lfo-pictures, docs/MPC.md#envelope-ui, 82917444, c8544168).
- **Modulation matrix**: A 64-slot matrix routing 57 internal modulation sources with bipolar depth, polarity, and bypass controls (docs/MPC.md#mod-matrix-and-macros, 1eafdf37, 6b191edb).
- **Tier low approximations**: Warped oscillators run 2x oversampled with a 32-tap Kaiser decimator; spectral morph cycles update every 512 samples; tanh uses a [7/6] Padé rational function (< 1e-4 error); sin/cos use a 1024-entry interpolated table; exp/pow/log2 use polynomial forms; filter coefficient calculations interpolate across 8-sample sub-blocks; inactive modulators step every 4th control period; voice stealing applies a 3 ms kill fade ramp (docs/MPC.md#sound-differences-at-tier-low, 4cd77ce, bc0f3ce, e3abe08, 178316e, e596f94, 91f2d75).
- **Bypassed vs active effects**: Tier low bypasses reverb, flanger, Hyper/Dimension, Bode frequency shifter, and Convolve; active effects are chorus, compressor, delay, distortion, EQ, effect filter, and phaser (docs/MPC.md#sound-differences-at-tier-low, quality.h).
- **SIMD determinism**: ARM NEON vector loops (`core/engine-own/dsp/simd.h`) compiled with `-fp-contract=off` produce bit-identical sample output to scalar code across all 112 factory presets on ARMv7 and AArch64 Linux (docs/MPC.md#mpc-xl-64-bit-aarch64).
- **Pad pressure integration**: Polyphonic aftertouch (MIDI 0xA0) adds a per-voice cutoff offset to Filter 1 (up to 36 semitones, smoothed with a 12 ms one-pole filter), leaving matrix presets without duplicate effects (docs/MPC.md#pad-pressure, 45_pad_pressure.py, test_own_pad_pressure.cpp).

### Presets
- **Factory content**: 112 factory presets in CC0 ContentPack format under `content/presets/` generated deterministically via `scripts/gen-factory-presets.py` (README.md#factory-content, docs/MPC.md#build, 0a46744f).
- **State format**: All presets store full state version 3 with 1484 explicit parameter keys (README.md#factory-content, 038b6538).
- **Level normalization**: Presets are calibrated to a chord loudness of -20 dBFS RMS, preserving peak headroom (README.md#factory-content, docs/MPC.md#output-level).
- **Showcase patches**: Includes 12 specialized showcase presets demonstrating version 3 DSP features, including Scream Filter Bass, Custom Shape Wobble, MPE Pressure Lead, Diffusor Cloud Pad, and Bode Shifter Drone (README.md#factory-content, 0a46744f).
- **Preset browsing**: Presets are exposed directly as VST2 programs in the MPC preset browser (docs/MPC.md#install-on-the-mpc).
- **Project persistence**: Presets and state are saved in MPC project files via VST chunk format with "chunk wins" protection against post-restore host program resets (docs/MPC.md#project-save-and-reload, play_restore_host.cpp).

### MPC Plugin Shell
- **Binary bundle**: Deployed in folder `Safian - VST - SynthyBoy` containing 32-bit ARMv7 `synthyboy-arm32.so` and 64-bit AArch64 `synthyboy-arm64.so` stripped VST2 binaries (docs/MPC.md#build, docs/MPC.md#package-layout).
- **Linkage compliance**: Links exclusively against system libc, libm, libdl, libpthread, and dynamic loader; no dynamic libstdc++; exports strictly `VSTPluginMain` (docs/MPC.md#build).
- **Crash isolation**: Wraps plugin initialization and all dispatcher calls in `catch (...)` handlers to prevent unhandled exceptions from terminating the host process (docs/MPC.md#observed-on-the-device).
- **Display thread**: Dedicated background display thread (`sb-lfo-display`, nice 0, SCHED_OTHER, 10 Hz) communicates parameter changes via `audioMasterAutomate` and updates display metadata via `audioMasterUpdateDisplay` without blocking audio processing (docs/MPC.md#lfo-pictures, lfo_live.h, thread_policy.h, 23e25c2d).
- **Parameter sequence**: Fixed sequence of 2719 parameters in overlay build: 2637 engine parameters, 4 LFO cursors (2637..2640), 32 spectrum meters (2641..2672), Pad Pressure (2673), Pad Pressure Amount (2674), Overlay Instance (2675), 3 play selectors (2676..2678), 24 play proxies (2679..2702), and 16 Q-Link proxies (2703..2718) (docs/MPC.md#parameter-order-fixed, 7da8db33, 98805c9d).
- **Output stage gain**: Hardware output stage applies a fixed +18 dB make-up gain (`SYNTHYBOY_MPC_OUTPUT_GAIN_DB=18`) with peak limiter optional and disabled by default (`SYNTHYBOY_MPC_OUTPUT_LIMITER=OFF`) (docs/MPC.md#output-level, 0e1f2100, b77d4fc7, 3c6abfbb, b56cc510, 2ee94841).
- **Browser artwork**: 270x110 PNG graphic tile installed under `Plugin Skins/browser_images/soundsmode.png` for Sounds > INSTRUMENTS browser (docs/MPC.md#browser-tile, gen-mpc-tile.py, 98_tile.py).

### Drawn UI Page by Page (Overlay Build)
- **Multi-plane display rendering**: Renders on an independent hardware overlay plane (plane 38 on Key 61, plane 46 on XL) positioned above host UI with runtime CRTC and plane discovery (docs/MPC.md#overlay-experimental, discovery.h).
- **GPU helper daemon**: Out-of-process rendering daemon (`synthyboy-overlay-gpu-arm32` / `arm64`) using OpenGL ES 2 and Panfrost drivers over UNIX seqpacket socket and memfd, falling back automatically to CPU rendering (docs/MPC.md#gpu-drawing-in-a-helper-process, scene.h, gpu_overlay.h).
- **Component library**: Standardized controls (`shell-mpc/ui/`) including Knob, NumberField, Stepper, UpDownStepper, ToggleIcon, MenuButton, ListPopup, DetailButton, RouteButton, PictogramSelector, and composite panels (docs/MPC.md#ui-components, 72436f75).
- **Play page (`PlayView`, sub-tabs Play, Play Env, Play LFO)**: Composite overview displaying live panels for oscillator cycle, envelope shape, and LFO curve with real-time voice cursors, instance selectors, and 24 proxy knobs (docs/MPC.md#play-page-overlay-build, 80107be9, play_view.h).
- **Filter pages (Filter 1 & 2, `FilterUi`)**: Visualizes real-time dB/log-frequency response curve (`FilterResponse`), interactive cutoff/resonance drag handles, formant pad, model lists, style steppers, and serial/parallel dual-curve combinations clipped cleanly at -48 dB display boundary (docs/MPC.md#filter-ui, a1166d2c, 1001).
- **Mixer page (`MixerUi`)**: 10 channel strips with volume faders, peak meters fed by `stripPeak`, top-down Q assignment (Level, Pan, Bus 1, Bus 2), and active routing indicators (docs/MPC.md#mixer-and-fx, 5a8cf0c7, 27d6b9bb, 1005).
- **FX page (`FxUi`)**: Visual effect chain bar, dedicated slot editor per effect type with parameter curves, and 13-type Quick-Add `FxGallery` when effect chain is empty (docs/MPC.md#mixer-and-fx, 5a8cf0c7, b8ac66ba, 1005).
- **Modulation Matrix (`ModUi`)**: Filterable by ALL / BY SOURCE / BY DEST, 72 px rows with depth bars and polarity/bypass toggles, slot detail editing, and 57-source Quick-Add board (`ModSourceBoard`) on empty state (docs/MPC.md#mod-matrix-and-macros, 1eafdf37, b8ac66ba, 1003).
- **Macros page (`MacroUi`)**: 8 macro control cards displaying active assignments alongside real-time output spectrum visualization; initializes on BANK 1 (docs/MPC.md#mod-matrix-and-macros, 3b8fc073, 1003).
- **Master page (`MasterUi`)**: Dual-sided view covering Main section (Output, Pitch bend, Voice allocation, Glide) and Tuning/MPE section (microtonal scale selector with dark touch keyboard, tuning systems, MPE parameters), with poly-cell active voice meter (docs/MPC.md#master-and-voice, d700b6f1, a5126580, c72b9f37, 1007).
- **Voice page (`VoiceUi`)**: 6 Q groups covering Step sequencer header, Random 1 & 2, Scaling, and Target groups across all 62 voice parameters (docs/MPC.md#master-and-voice, d700b6f1, c72b9f37, 1007).
- **Envelope pages (Env 1 - Env 4, `EnvUi`)**: Time-proportional DAHDSR graph with stepped auto-zoom (100 ms..50 s), interactive drag handles for stage times and curvature, live voice phase dots, targets list, and ADSR/Shape/Time Q groups (docs/MPC.md#envelope-ui, 99373e7e, 82b78e5c, 969).
- **LFO pages (LFO 1 - LFO 4 & Random LFO 1 - 4, `LfoUi`)**: Unipolar LFO shape graph with time-scale delay/fade strip, 13-cell shape picker grid as Q1, large Rate knob, uniform medium secondary knobs, and live voice phase cursor (docs/MPC.md#plugin-drawn-pages-overlay-build, 906cb667, cdc14c4c, 99373e7e, 1009, 1011).
- **Oscillator pages (Osc A, Osc B, Osc C, Sub, Noise, `OscPageUi`)**: Dedicated full-screen pages replacing old picture views, layout reflowed to ensure >= 240 px display height, 2D wavetable cycle display with bipolar fill and unison hairlines, type bar, vertical tuning column, detail popups, and ContentBrowser (docs/MPC.md#the-osc-tab, d557a512, 0a618f27, b78d9de8, 979).

### Hardware Control
- **16 Q-Link knobs**: 16 proxy parameters (`Q 1`..`Q 16`) mapped to page controls in reading order, wrapping around, with slots 1-4 mapped to active Q group (docs/MPC.md#q-proxies-and-the-plugin-drawn-pages, 7da8db33, 832).
- **Device detection**: Automatically detects 4 physical knobs on Key 61 vs 16 knobs on MPC XL via `/proc/device-tree/model` (docs/MPC.md#chips-on-the-screen, ecb776ad, 855). On Key 61, the hardware Bank button selects banks 1-4 (`Bank Direction "Row"`) (docs/MPC.md#measured-on-the-key-61, f2e133c7, 857).
- **Relative travel for stepped parameters**: Toggles, lists, and integer steppers advance one step per 3/128 of knob rotation instead of absolute positioning, avoiding excessive physical travel (docs/MPC.md#stepped-targets-are-driven-relatively, 01ac74a3, 853).
- **Host Q-Link panel handling**: Dynamic optical keying detects host Q-Link Status popup and clips overlay rendering along left edge, allowing host display to show through smoothly (docs/MPC.md#the-hosts-q-link-status-pop-up, 23e25c2d, da5eec7d, 902).
- **USB mouse support**: Connects to `/dev/input/event*` devices without exclusive grab; left click generates touch down/up, cursor position is determined from DRM atomic properties on host pointer plane, and mouse wheel steps parameters (docs/MPC.md#the-mouse-path, b9e331f0, 370c3895, 886).
- **Modulation control**: Interactive knob modulation chips allow depth dragging on touch screen; long-press opens ModulationsPage for routing (docs/MPC.md#modulation, 912).

### Known Problems and Limits
- **Denormal warning in SDK simulator**: `host_sim` reports 32 denormal samples during parameter sweeps with Vital engine due to simulator threshold discrepancy with IEEE 754; harmless on hardware (docs/MPC.md#denormal-warning-in-host-simulator, 727).
- **Key 61 Q-Link panel flicker**: Rapid knob turning previously caused reference drops and flickering on Key 61; fix implemented in software via keying logic and reading `/data/Settings/MPC/MPC.settings`, but unverified on hardware (docs/MPC.md#the-flicker-with-the-pop-up, b0b8cc69, 904).
- **Mouse cursor plane zpos on XL**: MPC XL uses mutable zpos; overlay plane must set zpos to 0 to prevent obscuring mouse pointer plane (docs/MPC.md#stacking-against-the-hosts-pointer, b9e331f0, 597).
- **Single overlay plane**: Multi-instance environments support overlay display for only one active instance at a time; instances beyond the 16th receive no overlay slot (docs/MPC.md#several-instances, 614).
- **Pad pressure limitation**: Full Level hardware button disables polyphonic aftertouch entirely (docs/MPC.md#full-level-must-be-off, 560).
- **Heavy patch performance**: Synthetic heavy patches (3 osc, 2 filters, FX, 8 notes) exceed the 2.902 ms block deadline on MPC Key 61 (average 3.35 ms), though all 112 factory presets run reliably within budget (docs/MPC.md#observed-on-the-device, 98).
- **Sample loader format constraints**: ContentBrowser supports WAV and SFZ files only; FLAC is unsupported, and SFZ files with relative directory traversal (`..`) are rejected (docs/MPC.md#the-content-browser, 958).

### In Progress
- **DAW embedding and touch validation**: Physical touch hardware validation and DAW embedding checks for desktop/tablet JUCE GUI (README.md#phase-5-ui, docs/ARCHITECTURE.md#391).
- **FLAC decoder integration**: FLAC decoding support for sample and granular oscillator loaders (docs/MPC.md#the-content-browser, 958).
- **Vital listening evaluation on hardware**: Final listening evaluation and performance benchmarking of the Vital engine default on physical MPC hardware (README.md#phase-2-vital-take-over, docs/ARCHITECTURE.md#422).
- **Release Phase 7**: Code signing, Apple notarization, and user manual compilation (README.md#phase-7-release).
- **Hardware mouse verification**: End-to-end hardware validation of USB mouse click and scroll interactions on physical MPC Key 61 and XL units (docs/MPC.md#the-mouse-path, 886).
