# SynthyBoy - Development History by Week

### Week 2026-W45 (2026-11-04 to 2026-11-08)
- Initial spike and Phase 1-2 foundation: core/shell architecture split, `IEngine` abstraction, `ParamInfo` parameter table, StateIO, Vital DSP import, JUCE removal, ARMv7 NEON port, and initial CI matrix.
- Phase 3 engine completion: expanded the parameter table with 604 new keys (state v3 in that week, 038b6538; the tree now has 2637 keys, state v4), wired the Serum filter types (69460e77), formula parsing, custom wavetable editing, sample oscillator, microtonal tuning, and MPE support.
- Phase 4 factory content: established ContentPack format and deterministically generated 112 CC0 factory presets (100 classics + 12 showcase presets) with macro routing and normalized chord levels.
- Quality gates and validation: added fuzz smoke testing, thread sanitizer coverage, preset sanity checks, and headless UI kit component testing with snapshot generation.

### Week 2026-W46 (2026-11-09 to 2026-11-15)
- MPC standalone build architecture: created dual ARMv7 32-bit and AArch64 64-bit VST2 package (`Safian - VST - SynthyBoy`), containerized Docker cross-compilation, and strict dynamic linking checks.
- Hardware profiling on MPC Key 61 (2026-11-13): tuned 8-voice polyphony, established unison cap 3, confirmed FTZ behavior, and proved audio callback stability (all gaps < 1.5x block time).
- Native MPC skin generation: built 30+ tab layouts from JSON, hardware Q-Link knob mapping, DAHDSR envelope curve widget, wavetable cycles, and LFO film-strip picture displays.
- Experimental hardware overlay: implemented direct DRM/KMS plane rendering (plane 38 on Key 61, plane 46 on XL), runtime CRTC/plane discovery, marker detection, and multi-instance slot tracking.
- Out-of-process GPU rendering helper (`synthyboy-overlay-gpu`) with OpenGL ES 2 Panfrost drivers, memfd shared memory, and seamless automatic CPU fallback.
- Live display features: live FilterResponse curves, 32-meter output spectrum analyzer, polyphonic pad pressure routing to Filter 1 Cutoff, 18 dB output make-up gain stage, and the 3-subtab Play page composite view.
- Plugin-drawn UI pages: introduced UI component library (`shell-mpc/ui/`), creating initial full-screen plugin-drawn pages for Filter, Mixer, FX, Modulation matrix, Master, and Voice.

### Week 2026-W47 (2026-11-16)
- Extended hardware Q-Link proxies to 16 parameters (`Q 1`..`Q 16`) at indices 2703..2718 with wrap-around group mapping, supporting 4 knobs on Key 61 and 16 knobs on MPC XL.
- Implemented relative travel accumulation for stepped Q targets (toggles, lists, steppers, 01ac74a3); not measured on a device.
- Added hardware USB mouse support via Linux input subsystem, reading pointer plane coordinates via atomic DRM properties and wheel scrolling.
- Serum-style envelope time-proportional graph with stepped auto-zoom and live voice phase dots; LFO unipolar display with 13-cell shape picker grid.
- Full-screen plugin-drawn OSC tab (`Osc A`, `Osc B`, `Osc C`, `Sub`, `Noise`) with reflowed >= 240 px display, wavetable cycles, and integrated ContentBrowser.
- Quick-Add boards for empty states: 57-source `ModSourceBoard` for modulation matrix and 13-card `FxGallery` for FX rack.
- Reworked the key for the host's Q-Link Status pop-up (reference only from a still strip, option read from `MPC.settings`, 23e25c2d); the Key 61 flicker is a known open bug, not confirmed fixed on the device (b0b8cc69). The program name keeps its modification mark (`*`).
