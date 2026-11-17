# SynthyBoy - MPC Release Change Log (Draft)

SynthyBoy for the MPC is a synthesizer plugin (VST2) for standalone Akai MPC hardware, packaged as `Safian - VST - SynthyBoy` with a 32-bit ARMv7 and a 64-bit AArch64 build; it was tried by its author on the MPC Key 61 and the MPC XL only (docs/MPC.md#package-layout, docs/MPC.md#overlay-experimental).
It runs the project's own sound engine at quality tier low, which on the MPC means 8 voices of polyphony and at most 3 unison voices per oscillator (docs/MPC.md#sound-differences-at-tier-low, c3a2065); re-read this line when the per-device quality profiles are merged.
Its screens are drawn by the plugin itself on an experimental display overlay, and the hardware Q-Link knobs are mapped to the controls of the page on screen (docs/MPC.md#overlay-experimental, docs/MPC.md#q-proxies-and-the-plugin-drawn-pages-overlay-build).
The instrument has 136 factory presets, 3 oscillators (wavetable, sample, granular, spectral, multisample), a sub oscillator and a noise source, 27 filter models in 12 styles, 6 envelopes, 10 LFOs and 4 random LFOs, a 64-slot modulation matrix and 13 effect types (docs/params.json, docs/MPC.md#filter-ui, docs/MPC.md#mod-matrix-and-macros).
The same codebase also builds the desktop plugin (VST3, CLAP, Audio Unit); this change log covers the MPC release only (README.md#synthyboy).

---

## State of the MPC Build

### Sound Engine
- **Engine codebase**: The MPC build runs SynthyBoy's own engine (`core/engine-own`), an original C++17 DSP codebase completely free of JUCE and isolated from GPL Vital source code (docs/ARCHITECTURE.md#22-licence-hygiene, core/engine-own/README.md).
- **Execution tier**: The engine runs strictly at `Tier::low`, enforcing an 8-voice polyphony limit and a unison cap of at most 3 unison voices per oscillator (`core/include/synthyboy/quality.h`, docs/MPC.md#sound-differences-at-tier-low, c3a2065, 91f2d75).
- **Oscillator models**: Features 3 main oscillators supporting Wavetable (with spectral morph and two warp slots), Sample, Granular, Spectral, and Multisample modes, plus a dedicated Sub oscillator (6 shapes) and Noise source (9 noise types / sample) (docs/MPC.md#oscillator-ui, docs/ARCHITECTURE.md#32-oscillators, d90e1a0a, 42682c52).
- **Filter collection**: Includes 2 main filters and an effect filter with 27 models in 12 styles (Analog, Dirty, Ladder, Digital, Diode, Formant, Comb, Phaser, Scream, etc.; docs/params.json `filter1.model`, `filter1.style`) and parallel, series 1 to 2, series 2 to 1 routing (`filter.routing`) (docs/params.json, docs/MPC.md#filter-ui, 69460e77, a1166d2c).
- **Modulator complement**: Provides 6 envelopes with adjustable curvature parameters, 10 LFOs with custom shape support, and 4 random LFOs (docs/MPC.md#lfo-pictures, docs/MPC.md#envelope-ui, 82917444, c8544168).
- **Modulation matrix**: A 64-slot matrix routing 57 internal modulation sources with bipolar depth, polarity, and bypass controls (docs/MPC.md#mod-matrix-and-macros, 1eafdf37, 6b191edb).
- **Tier low approximations**: Warped oscillators run 2x oversampled with a 32-tap Kaiser decimator; spectral morph cycles update every 512 samples; tanh uses a [7/6] Padé rational function (< 1e-4 error); sin/cos use a 1024-entry interpolated table; exp/pow/log2 use polynomial forms; filter coefficient calculations interpolate across 8-sample sub-blocks; inactive modulators step every 4th control period; voice stealing applies a 3 ms kill fade ramp (docs/MPC.md#sound-differences-at-tier-low, 4cd77ce, bc0f3ce, e3abe08, 178316e, e596f94, 91f2d75).
- **Bypassed vs active effects**: Tier low bypasses reverb, flanger, Hyper/Dimension, Bode frequency shifter, and Convolve; active effects are chorus, compressor, delay, distortion, EQ, effect filter, and phaser (docs/MPC.md#sound-differences-at-tier-low, quality.h).
- **SIMD determinism**: ARM NEON vector loops (`core/engine-own/dsp/simd.h`) compiled with `-fp-contract=off` produce bit-identical sample output to scalar code across all 112 factory presets on ARMv7 and AArch64 Linux (docs/MPC.md#mpc-xl-64-bit-aarch64).
- **Pad pressure integration**: Polyphonic aftertouch (MIDI 0xA0) adds a per-voice cutoff offset to Filter 1 (up to 36 semitones, smoothed with a 12 ms one-pole filter), leaving matrix presets without duplicate effects (docs/MPC.md#pad-pressure, 45_pad_pressure.py, test_own_pad_pressure.cpp).

### Presets
- **Factory content**: 136 factory presets (the first 112 and the 24 of sound set batch 1, docs/release/SOUND-SET-BATCH-1.md) in CC0 ContentPack format under `content/presets/` generated deterministically via `scripts/gen-factory-presets.py` (README.md#factory-content, docs/MPC.md#build, 0a46744f).
- **State format**: Each preset is a `synthyboy-preset` file (format 1) holding a `synthyboy-state` version 4 with all 2637 parameter keys explicit, the same count and state version as the parameter table (`content/presets/Bass/scream_filter_bass.sbpreset`, docs/params.json `count` and `stateVersion`; 038b6538 introduced state v3, since raised).
- **Level normalization**: Presets are calibrated to a chord loudness of -20 dBFS RMS, preserving peak headroom (README.md#factory-content, docs/MPC.md#output-level).
- **Showcase patches**: Includes 12 specialized showcase presets demonstrating version 3 DSP features, including Scream Filter Bass, Custom Shape Wobble, MPE Pressure Lead, Diffusor Cloud Pad, and Bode Shifter Drone (README.md#factory-content, 0a46744f).
- **Preset browsing**: Presets are exposed directly as VST2 programs in the MPC preset browser (docs/MPC.md#install-on-the-mpc).
- **Project persistence**: Presets and state are saved in MPC project files via VST chunk format with "chunk wins" protection against post-restore host program resets (docs/MPC.md#project-save-and-reload, play_restore_host.cpp).

### MPC Plugin Shell
- **Binary bundle**: Deployed in folder `Safian - VST - SynthyBoy` containing 32-bit ARMv7 `synthyboy-arm32.so` and 64-bit AArch64 `synthyboy-arm64.so` stripped VST2 binaries (docs/MPC.md#build, docs/MPC.md#package-layout).
- **Linkage compliance**: Links exclusively against system libc, libm, libdl, libpthread, and dynamic loader; no dynamic libstdc++; exports strictly `VSTPluginMain` (docs/MPC.md#build).
- **Crash isolation**: Wraps plugin initialization and all dispatcher calls in `catch (...)` handlers to prevent unhandled exceptions from terminating the host process (docs/MPC.md#observed-on-the-device).
- **Display thread**: One display thread per plugin instance (normal priority: `SCHED_OTHER`, nice 0) reports parameter changes to the host with `audioMasterAutomate` and tells it to re-read names with `audioMasterUpdateDisplay`; the audio thread does not call the host (docs/MPC.md#known-host-behaviour, thread_policy.h, 23e25c2d).
- **Parameter sequence**: Fixed sequence of 2719 parameters in overlay build: 2637 engine parameters, 4 LFO cursors (2637..2640), 32 spectrum meters (2641..2672), Pad Pressure (2673), Pad Pressure Amount (2674), Overlay Instance (2675), 3 play selectors (2676..2678), 24 play proxies (2679..2702), and 16 Q-Link proxies (2703..2718) (docs/MPC.md#parameter-order-fixed, 7da8db33, 98805c9d).
- **Output stage gain**: Hardware output stage applies a fixed +18 dB make-up gain (`SYNTHYBOY_MPC_OUTPUT_GAIN_DB=18`) with peak limiter optional and disabled by default (`SYNTHYBOY_MPC_OUTPUT_LIMITER=OFF`) (docs/MPC.md#output-level, 0e1f2100, b77d4fc7, 3c6abfbb, b56cc510, 2ee94841).
- **Browser artwork**: 270x110 PNG graphic tile installed under `Plugin Skins/browser_images/soundsmode.png` for Sounds > INSTRUMENTS browser (docs/MPC.md#browser-tile, gen-mpc-tile.py, 98_tile.py).

### Drawn UI Page by Page (Overlay Build)
- **Multi-plane display rendering**: Renders on an independent hardware overlay plane (found at run time; the measurements used plane 38 on the Key 61 and plane 46 on the XL) positioned above host UI with runtime CRTC and plane discovery (docs/MPC.md#overlay-experimental, discovery.h).
- **GPU helper daemon**: Out-of-process rendering daemon (`synthyboy-overlay-gpu-arm32` / `arm64`) using OpenGL ES 2 and Panfrost drivers over UNIX seqpacket socket and memfd, falling back automatically to CPU rendering (docs/MPC.md#gpu-drawing-in-a-helper-process, scene.h, gpu_overlay.h).
- **Component library**: Standardized controls (`shell-mpc/ui/`) including Knob, NumberField, Stepper, UpDownStepper, ToggleIcon, MenuButton, ListPopup, DetailButton, RouteButton, PictogramSelector, and composite panels (docs/MPC.md#ui-components, 72436f75).
- **Play page (`PlayView`, sub-tabs Play, Play Env, Play LFO)**: Composite overview displaying live panels for oscillator cycle, envelope shape, and LFO curve with real-time voice cursors, instance selectors, and 24 proxy knobs (docs/MPC.md#play-page-overlay-build, 80107be9, play_view.h).
- **Filter pages (Filter 1 & 2, `FilterUi`)**: Visualizes real-time dB/log-frequency response curve (`FilterResponse`), interactive cutoff/resonance drag handles, formant pad, model lists, style steppers, and serial/parallel dual-curve combinations clipped cleanly at -48 dB display boundary (docs/MPC.md#filter-ui, a1166d2c).
- **Mixer page (`MixerUi`)**: 10 channel strips with volume faders, peak meters fed by `stripPeak`, top-down Q assignment (Level, Pan, Bus 1, Bus 2), and active routing indicators (docs/MPC.md#mixer-and-fx, 5a8cf0c7, 27d6b9bb).
- **FX page (`FxUi`)**: Visual effect chain bar, dedicated slot editor per effect type with parameter curves, and 13-type Quick-Add `FxGallery` when effect chain is empty (docs/MPC.md#mixer-and-fx, 5a8cf0c7, b8ac66ba).
- **Modulation Matrix (`ModUi`)**: Filterable by ALL / BY SOURCE / BY DEST, 72 px rows with depth bars and polarity/bypass toggles, slot detail editing, and 57-source Quick-Add board (`ModSourceBoard`) on empty state (docs/MPC.md#mod-matrix-and-macros, 1eafdf37, b8ac66ba).
- **Macros page (`MacroUi`)**: 8 macro control cards displaying active assignments alongside real-time output spectrum visualization; initializes on BANK 1 (docs/MPC.md#mod-matrix-and-macros, 3b8fc073).
- **Master page (`MasterUi`)**: Dual-sided view covering Main section (Output, Pitch bend, Voice allocation, Glide) and Tuning/MPE section (microtonal scale selector with dark touch keyboard, tuning systems, MPE parameters), with poly-cell active voice meter (docs/MPC.md#master-and-voice, d700b6f1, a5126580, c72b9f37).
- **Voice page (`VoiceUi`)**: 6 Q groups covering Step sequencer header, Random 1 & 2, Scaling, and Target groups across all 62 voice parameters (docs/MPC.md#master-and-voice, d700b6f1, c72b9f37).
- **Envelope pages (Env 1 - Env 4, `EnvUi`)**: Time-proportional DAHDSR graph with stepped auto-zoom (100 ms..50 s), interactive drag handles for stage times and curvature, live voice phase dots, targets list, and ADSR/Shape/Time Q groups (docs/MPC.md#envelope-ui, 99373e7e, 82b78e5c).
- **LFO pages (LFO 1 - LFO 4 & Random LFO 1 - 4, `LfoUi`)**: Unipolar LFO shape graph with time-scale delay/fade strip, 13-cell shape picker grid as Q1, large Rate knob, uniform medium secondary knobs, and live voice phase cursor (docs/MPC.md#plugin-drawn-pages-overlay-build, 906cb667, cdc14c4c, 99373e7e).
- **Oscillator pages (Osc A, Osc B, Osc C, Sub, Noise, `OscPageUi`)**: Dedicated full-screen pages replacing old picture views, layout reflowed to ensure >= 240 px display height, 2D wavetable cycle display with bipolar fill and unison hairlines, type bar, vertical tuning column, detail popups, and ContentBrowser (docs/MPC.md#oscillator-ui, d557a512, 0a618f27, b78d9de8).

### Hardware Control
- **16 Q-Link knobs**: 16 proxy parameters (`Q 1`..`Q 16`) mapped to page controls in reading order, wrapping around, with slots 1-4 mapped to active Q group (docs/MPC.md#q-proxies-and-the-plugin-drawn-pages-overlay-build, 7da8db33).
- **Device detection**: Automatically detects 4 physical knobs on Key 61 vs 16 knobs on MPC XL via `/proc/device-tree/model` (docs/MPC.md#chips-on-the-screen, ecb776ad). On Key 61, the Q-Link button cycles the four banks (`Bank Direction "Row"`) (docs/MPC.md#measured-on-the-key-61, f2e133c7).
- **Relative travel for stepped parameters**: Toggles, lists, and integer steppers are stepped by the accumulated travel of the knob instead of being set by its absolute position (docs/MPC.md#q-proxies-and-the-plugin-drawn-pages-overlay-build, 01ac74a3; not measured on a device, and the amount of travel per step is being re-calibrated).
- **Host Q-Link panel handling**: Dynamic optical keying detects host Q-Link Status popup and clips overlay rendering along left edge, allowing host display to show through smoothly (docs/MPC.md#the-hosts-q-link-status-pop-up, 23e25c2d, da5eec7d).
- **USB mouse support**: Connects to `/dev/input/event*` devices without exclusive grab; left click generates touch down/up, cursor position is determined from DRM atomic properties on host pointer plane, and mouse wheel steps parameters (docs/MPC.md#the-mouse-path, b9e331f0, 370c3895).
- **Plane order under the host's pointer**: Where the driver has a mutable `zpos` (the XL), the overlay puts its plane directly above the host's picture and below the host's pointer plane so that the pointer stays visible; the XL's plane table was measured, the rule itself is not verified on a device (docs/MPC.md#overlay-experimental, 397c0654).
- **Modulation control**: Interactive knob modulation chips allow depth dragging on touch screen; long-press opens ModulationsPage for routing (docs/MPC.md#modulation).

### Known Problems and Limits
- **Denormal warning in SDK simulator**: `host_sim` reports 32 denormal samples during parameter sweeps with Vital engine due to simulator threshold discrepancy with IEEE 754; harmless on hardware (docs/MPC.md#denormal-warning-in-host-simulator).
- **Key 61 Q-Link panel flicker**: Rapid knob turning previously caused reference drops and flickering on Key 61; the key was reworked in code and reads the host option from `MPC.settings`, but the flicker is a known open bug: the rework is verified by host tests only, not on the device; the user-side way around it is to switch the host's pop-up off (docs/MPC.md#plugin-drawn-pages-overlay-build, b0b8cc69, 909).
- **Mouse pointer on the XL**: the zpos rule and the pointer hot spot of the XL are not measured on a device (only the Key 61 has a measured hot spot in `mouse_router.h`) (docs/MPC.md#overlay-experimental, 597, 891).
- **Single overlay plane**: Multi-instance environments support overlay display for only one active instance at a time; instances beyond the 16th receive no overlay slot (docs/MPC.md#several-instances).
- **Pad pressure limitation**: Full Level hardware button disables polyphonic aftertouch entirely (docs/MPC.md#full-level-must-be-off).
- **Heavy patch performance**: Synthetic heavy patches (3 osc, 2 filters, FX, 8 notes) exceed the 2.902 ms block deadline on MPC Key 61 (average 3.35 ms), though all 112 factory presets run reliably within budget (docs/MPC.md#observed-on-the-device).
- **Sample loader format constraints**: ContentBrowser supports WAV and SFZ files only; FLAC is unsupported, and SFZ files with relative directory traversal (`..`) are rejected (docs/MPC.md#the-content-browser).

### In Progress
Branches that are open next to the MPC release line (`git branch --no-merged feature/mpc-overlay`), and what docs/MPC.md marks as not measured:
- **Per-device quality profiles**: branch `work/xl-tier` (94f5e3dd: quality profile per device, a bench that measures every setting).
- **LED-style controls (toggles and lists)** on the Voice, Filter, FX pages and the Mod slot page: branch `work/led-controls` (77f74d2e, 76ffa60f).
- **Editable macro names stored in the preset**: branch `work/macro-names` (331af461).
- **Standby dim handling of the overlay**: branch `work/standby-audit` (5c470640).
- **Q router follow-ups** (one read per write, new target restarts the stepping, stepped targets as plain knob for the host): branch `work/round6` (389944b1, 2b3555b7).
- **Robustness review and fixes**: branches `work/robustness-review`, `work/robustness-fixes` (f9b1310f).
- **In-host drawing**, a second presentation path (build switch off by default): branch `work/inhost-draw` (f5b13a93).
- **Device rounds on the MPC XL**: not measured on the XL according to docs/MPC.md: the Q-Link table with 16 knobs and the chips 5 .. 16 (docs/MPC.md#q-proxies-and-the-plugin-drawn-pages-overlay-build, 862), the stepped-target travel (858), the stacking rule (597), the pointer hot spot (891).
