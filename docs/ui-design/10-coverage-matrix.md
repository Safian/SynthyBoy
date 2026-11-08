# Coverage matrix – Serum 2 and Vital vs. SynthyBoy UI design

Proof that every Serum 2 and Vital function is either designed in files 01-09 / 11 (status HAVE / PARTIAL / GAP) or explicitly excluded. Citation style as in [00-BRIEF.md](00-BRIEF.md): `[S p.n]` Serum 2 manual v2.0.18, `[V p.n]` unofficial Vital manual. File refs: `02 §1.2` = [02-oscillators.md](02-oscillators.md) section 1.2. Keys only from `docs/params.json` (880); `N` / `NN` = index (`osc1..3`, `modNN` = `mod01..mod64`).

Status legend:

| Status | Meaning |
|--------|---------|
| HAVE | engine key / event / UI-only function exists and is designed |
| PARTIAL | part of the function is HAVE, the rest GAP (note says which) |
| GAP | engine lacks it; UI is designed anyway (disabled badge "not available in this engine"); listed in section 3 |
| EXCLUDED | out of scope: Kick (browser, clips, arp, grid/sequencer) or not a function (installer, licence, credits) |

Method notes: the Serum TOC text (pp.3-12) lost the Delay module line; Delay is taken from the manual body [S p.165]. Subsection pages that the TOC text does not give are taken from the area files' citations. The Vital manual has 43 pages, not 8 (see X7). Row counts: Serum 182, Vital 68.

## 1. Serum 2 matrix

### 1.1 Welcome, Exploring, Getting Started, Basic Operations (pp.13-36)

| Serum function | Our design (file §) | Our keys | Status | Note |
|----------------|---------------------|----------|--------|------|
| Registering / Getting in touch / Downloading / Installing / Uninstalling [S p.13-15] | - | - | EXCLUDED | not a function (store / host handles install and licence) |
| Exploring Serum, sound design overview, tabs OSC / MIX / FX / MOD / GLOBAL [S p.16] | 01 §1.2, §2 | none | HAVE | page tabs; CLIP / ARP tabs excluded |
| Adding Serum to a track [S p.18] | - | - | EXCLUDED | host business |
| Loading a preset, preset name prev / next [S p.18] | 01 §1.1 | none | HAVE | prev / next only; browser = Kick |
| Creating a new sound = Init Preset [S p.20] | 01 §1.1 | all keys to default | HAVE | |
| Saving changes, ARTIST / DESC metadata [S p.21] | 01 §1.1 | none (`.sbpreset` metadata) | HAVE | |
| Embedding content when saving a preset [S p.22] | 01 §1.1, 03 §1.3 | none | PARTIAL | wavetable embed GAP in 03 (X3); sample embed GAP (no sample osc) |
| Dragging audio to your DAW [S p.22] | 01 §1.1 | none | GAP | G03: mobile = Share button |
| Displaying help (tooltips) [S p.23] | 01 §1.4 | none | HAVE | UI |
| Using the Serum keyboard [S p.25] | 01 §1.3 | NoteOn / NoteOff events | HAVE | touch velocity, latch, octave shift |
| Using knobs and sliders: drag, fine, wheel, numeric entry, reset [S p.25-26] | 01 §1.4 | any key | HAVE | long press = value / assign / reset / MIDI-learn menu |
| MIDI Learn / Remove MIDI CC / Lock Parameter [S p.26] | 01 §1.4, 07 §1 | none (shell state) | HAVE | G07 built as a shell-side table (15-shell-features.md 2): long-press sheet -> MIDI learn / Remove CC; lock = G08 (module lock, 15 section 4) |
| Undo and Redo [S p.27] | 01 §1.1 | any key, shapes | HAVE | UI; A/B compare added |
| Controlling the main output volume + meter [S p.27] | 01 §1.1, 07 §1 | `master.volume` | HAVE | |
| Enabling an oscillator or filter [S p.28] | 02 §1.1, 04 §1.1 | `osc1.on`, `osc2.on`, `osc3.on`, `filter1.on`, `filter2.on`, `noise.on`, `fxfilter.on` | HAVE | Sub osc: GAP (G23) |
| Choosing oscillator / filter options (menus, arrows) [S p.29] | 01 §1.4 | stepped keys (`osc1.wavetable`, `filter1.model`, ...) | HAVE | |
| Pitch controls OCT / SEM / FIN [S p.29] | 02 §1.1 | `osc1.transpose`, `osc1.tune` | HAVE | |
| Pitch CRS, modes Harmonics / Ratio / Step, tracking toggle [S p.29-36] | 02 §1.1 | none | GAP | G25, G26, G27 |
| Routing an oscillator or filter: Filter 1 / 2 / both, Main, Direct [S p.31] | 02 §1.1, 04 §1.1 | `osc1.destination`, `noise.destination`, `filter.routing` | PARTIAL | knob blend, None, Bus 1/2 = G28 |
| Oscillator / filter output as modulation source [S p.208] | 02 §1.1, 06 §1 | none | GAP | G29 |
| Accessing the module menu: Lock, Init, Copy, Paste, Copy w/ mods, swap [S p.33-34] | 01 §1.4 | all keys of the module + its `modNN` | HAVE | Lock = G08, built (15-shell-features.md 4) |
| Resizing the UI [S p.36] | 01 §1.1 | none | HAVE | UI scale; phone/tablet layouts |

### 1.2 Wavetable oscillator (pp.37-57)

| Serum function | Our design (file §) | Our keys | Status | Note |
|----------------|---------------------|----------|--------|------|
| What is a wavetable / anatomy (<= 256 frames, 2048 samples) [S p.38] | 02 §1.2, 03 §1.1 | `osc1.wavetable`, `osc1.frame` | PARTIAL | built-ins have 1 or 64 frames; G30 |
| Waveform display 2D / 3D, spectral view [S p.39] [V p.7] | 03 §1.1 | `osc1.frame`, `osc1.wavetable` | HAVE | UI draws built-in table data |
| Choose wavetable (menu, arrows) [S p.40] | 02 §1.2 | `osc1.wavetable` | PARTIAL | 13 built-ins; user tables G24 |
| Phase [S p.41] | 02 §1.2 | `osc1.phase` | HAVE | |
| Randomizing the phase [S p.42] | 02 §1.2 | `osc1.random_phase` | HAVE | |
| Phase Legato [S p.43] | 02 §1.2 | none | GAP | G31 |
| Setting phase memory [S p.44] | 02 §1.2 | none | GAP | G31 |
| Wavetable position, smooth interpretation [S p.45] | 02 §1.2 | `osc1.frame` | PARTIAL | smooth switch = tier (G30) |
| Unison voices [S p.45] | 02 §1.2 | `osc1.unison.voices` | HAVE | tier caps 4 / 8 / 16 |
| Unison detune amount, curve [S p.46-48] | 02 §1.2 | `osc1.unison.detune`, `osc1.unison.detune_curve` | PARTIAL | 5 discrete MODE laws and RANGE = G32 |
| Unison stack, width, blend [S p.47-48] | 02 §1.2 | `osc1.unison.stack`, `osc1.unison.spread`, `osc1.unison.blend` | HAVE | stack is a superset; repeat count 1-3x = G32 |
| Unison WT POS / WARP 1 spread [S p.48] | 02 §1.2 | `osc1.frame_spread`, `osc1.warp.spread` | HAVE | |
| Unison WARP 2 spread [S p.48] | 02 §1.2 | none | GAP | G32 (one `warp` + one `morph` slot only) |
| Warp: OFF, amount [S p.49] | 02 §1.2 | `osc1.warp.type`, `osc1.warp.amount` | HAVE | |
| Warp slot 2 / Swap Warps [S p.49, p.56] | 02 §1.2 | `osc1.morph.type`, `osc1.morph.amount` | PARTIAL | morph = fixed second family; free slots G33 |
| Warp Sync (+ Var), Alt group, PWM, Quantize, Formant, Squeeze [S p.50-51] [V p.10] | 02 §1.2 | `osc1.warp.type`, `osc1.warp.amount`, `osc1.warp.phase` | PARTIAL | Sync, Bend, Pulse, Quantize, Formant, Squeeze HAVE; Var, Bend +/-, Asym, Flip, Mirror, Remap, Odd/Even = G34 |
| Warp Filter group LPF / HPF, Distortion group (14 modes) [S p.52-53] | 02 §1.2 | none (spectral LP/HP exist as morph types: `osc1.morph.type`) | GAP | G35 |
| Warp FM / RM from other oscillators [S p.54-56] [V p.10] | 02 §1.2 | `osc1.warp.type` | PARTIAL | FM / RM from Osc x HAVE; from Noise, Sub, Filters, sampler, Thru-Zero / Exp / Linear, PD, AM = G29, G36, G37 |
| Wavetable pan, level [S p.57] | 02 §1.1 | `osc1.pan`, `osc1.level` | HAVE | |

### 1.3 Multisample, Sample, Granular, Spectral oscillators (pp.58-122)

| Serum function | Our design (file §) | Our keys | Status | Note |
|----------------|---------------------|----------|--------|------|
| Multisample: select instrument / SFZ [S p.58-59] | 02 §1.3 | none | GAP | G19 |
| Multisample: zone display [S p.60] | 02 §1.3 | none | GAP | G19 |
| Multisample envelope (OVERRIDE, DELAY, A, H, D, S, R) [S p.61-62] | 02 §1.3 | none | GAP | G19 |
| Multisample VEL TRACK, RAND [S p.62] | 02 §1.3 | none | GAP | G19 |
| Multisample TIMBRE, unison, warp, pan, level [S p.63-66] | 02 §1.3 | `osc1.pan`, `osc1.level` (rest none) | GAP | G19 |
| Switch last note played to a sample [S p.67] | 02 §1.3 | none | GAP | G19 |
| Sample: select sampler, root note [S p.68-70] | 02 §1.4 | none | GAP | G20 |
| Sample: start / end [S p.71] | 02 §1.4 | none | GAP | G20 |
| Sample: operations, snap, fade, normalize, reverse, trim [S p.72-73] | 02 §1.4 | none | GAP | G20 |
| Sample: slicing (auto / manual), slice options [S p.74-76] | 02 §1.4 | none | GAP | G20; send to clip = EXCLUDED Kick |
| Sample: loop menu, loop start / end, crossfade [S p.77-79] | 02 §1.4 | none | GAP | G20 |
| Sample: scan, unison START / SPAN, warp, pan, level [S p.80-84] | 02 §1.4 | `osc1.pan`, `osc1.level` (rest none) | GAP | G20 |
| Switch sample to wavetable [S p.84] | 02 §1.4, 03 §1.4 | none | GAP | G20, G51 |
| Granular: select, start / end, operations, loop, crossfade [S p.85-90] | 02 §1.5 | none | GAP | G21 |
| Granular: window amount / skew / shape [S p.91-92] | 02 §1.5 | none | GAP | G21 |
| Granular: unison START / SPAN / SPAWN pattern [S p.93-95] | 02 §1.5 | none | GAP | G21 |
| Granular: X\|Y control [S p.96-98] | 02 §1.5 | none | GAP | G21 |
| Granular: warp mode, scan, density, length, pan, level [S p.99-102] | 02 §1.5 | `osc1.pan`, `osc1.level` (rest none) | GAP | G21 |
| Granular: grain randomization [S p.102] | 02 §1.5 | none | GAP | G21 |
| Granular: switch to wavetable [S p.103] | 02 §1.5 | none | GAP | G21, G51 |
| Spectral: select, start / end, high / low frequency markers [S p.104-107] | 02 §1.6 | none | GAP | G22 |
| Spectral: operations, loop, crossfade, unison [S p.108-113] | 02 §1.6 | none | GAP | G22 |
| Spectral: X\|Y control [S p.114-115] | 02 §1.6 | none | GAP | G22 |
| Spectral: scan, cut, filter curve, mix, warp mode, pan, level [S p.116-121] | 02 §1.6 | `osc1.pan`, `osc1.level` (rest none) | GAP | G22 |
| Spectral: switch to wavetable [S p.122] | 02 §1.6 | none | GAP | G22, G51 |

### 1.4 Sub, Noise, Filters, Mixer (pp.123-151)

| Serum function | Our design (file §) | Our keys | Status | Note |
|----------------|---------------------|----------|--------|------|
| Sub: pitch, waveform, phase, pan, level [S p.123-127] | 02 §1.7 | none | GAP | G23 (Vital engine has `sub_*`, not exposed) |
| Noise: enable, level, pan, routing [S p.128-132] | 02 §1.8 | `noise.on`, `noise.level`, `noise.pan`, `noise.destination` | HAVE | white noise only |
| Noise: load preset / sample, one-shot / looping, start, random, pitch, fine [S p.129-132] | 02 §1.8 | none | GAP | G38 |
| Filter modules: routing (S, A, B, C, N) [S p.134] | 04 §1.1 | `osc1.destination`, `noise.destination`, `filter.routing` | HAVE | Sub input GAP (G23) |
| Filter type menu and Var parameter [S p.135-139] | 04 §1.1, §1.3, §2.3 | `filter1.model`, `filter1.style`, `filter1.blend` | PARTIAL | 8 models x 6 styles; about 35 of 60 Serum types = G58 |
| Drawable PZ SVF [S p.139] | 04 §1.1 | none | GAP | G57 |
| Filter display options (response, FFT, phase) [S p.140-141] | 04 §1.1 | `filter1.cutoff`, `filter1.resonance`, `filter1.blend` | HAVE | UI draws |
| Cutoff, resonance [S p.141-142] | 04 §1.1 | `filter1.cutoff`, `filter1.resonance` | HAVE | |
| Drive, Clean Mode [S p.142] | 04 §1.1 | `filter1.drive` | PARTIAL | Clean Mode = G54 |
| Fat (and others) = per-type 4th knob [S p.142] | 04 §1.2 | `filter1.keytrack`, `filter1.comb_offset`, `filter1.formant.x`, `filter1.formant.y`, `filter1.formant.resonance`, `filter1.formant.spread`, `filter1.formant.transpose` | HAVE | per model |
| Filter pan, level [S p.143] | 04 §1.1 | none | GAP | G55 |
| Filter mix [S p.143] | 04 §1.1 | `filter1.mix` | HAVE | |
| Keytrack following oscillator pitch [S p.142] | 04 §1.1 | `filter1.keytrack` | PARTIAL | knob HAVE, follow-osc-pitch G56 |
| Mixer: exploring, channels, toggle [S p.144-145] | 02 §1.9 | `osc1.on`, `noise.on`, `filter1.on` | HAVE | |
| Mixer: sending to busses, ENV 1 button [S p.146] | 02 §1.9 | `osc1.destination`, `noise.destination` | PARTIAL | discrete Main / Direct; Bus 1/2, ENV1 toggle = G39 |
| Mixer: pan and levels [S p.147] | 02 §1.9 | `osc1.pan`, `osc1.level`, `noise.pan`, `noise.level` | HAVE | |
| Mixer: mixing filters (routing, pan, mix, levels) [S p.148-149] | 02 §1.9, 04 §1.1 | `filter.routing`, `filter1.mix`, `filter2.mix` | PARTIAL | series / parallel only; per-filter outputs, pan, level = G39, G55 |
| Mixer: mixing the busses, levels [S p.150] | 02 §1.9 | none | GAP | G39 |
| Mixer: main and direct levels [S p.151] | 02 §1.9 | `master.volume` | PARTIAL | DIRECT level G39 |

### 1.5 Serum FX (pp.152-182)

| Serum function | Our design (file §) | Our keys | Status | Note |
|----------------|---------------------|----------|--------|------|
| Using the FX module, selecting a rack (MAIN, BUS 1, BUS 2) [S p.152-153] | 08 §1.1 | none | PARTIAL | single rack; busses G70 |
| Loading / saving rack presets [S p.154, p.157] | 08 §1.1 | `fx.order.1` .. `fx.order.9` + all `<fx>.*` | HAVE | UI partial-state preset |
| Adding modules [S p.155] | 08 §1.1 | none | PARTIAL | fixed 9 slots, one instance per effect; G71 |
| Reordering modules [S p.156] | 08 §2.1 | `fx.order.1` .. `fx.order.9` | HAVE | |
| Copying a module [S p.157] | 08 §1.1 | none | PARTIAL | copy settings HAVE (UI); multi-instance G71 |
| Bypassing a module [S p.157] | 08 §1.1 | `chorus.on`, `compressor.on`, `delay.on`, `distortion.on`, `eq.on`, `flanger.on`, `phaser.on`, `reverb.on`, `fxfilter.on` | HAVE | bypass-all = UI |
| Removing a module / saving a rack as preset [S p.157] | 08 §1.1 | `fx.order.1` .. `fx.order.9` | PARTIAL | remove = bypass; delete G71 |
| FX rack operations (cut / copy / paste / clear bus, lock) [S p.158] | 08 §1.1 | `fx.order.1` .. `fx.order.9` | PARTIAL | lock = G08 |
| Modulating FX parameters [S p.159] | 08 §1.1 | modulatable FX keys (e.g. `reverb.mix`, `delay.mix`) | HAVE | 74 destinations |
| FX module operations, LEVEL trim [S p.160] | 08 §1.1 | none | GAP | G72 |
| Bode frequency shifter [S p.161] | 08 §1.2 | none | GAP | G78 |
| Chorus [S p.161] | 08 §1.2 | `chorus.on`, `chorus.mix`, `chorus.rate`, `chorus.sync`, `chorus.division`, `chorus.depth`, `chorus.feedback`, `chorus.delay1`, `chorus.delay2`, `chorus.voices`, `chorus.filter_cutoff`, `chorus.filter_spread` | PARTIAL | LEVEL G72 |
| Compressor [S p.162] | 08 §1.2 | `compressor.on`, `compressor.mix`, `compressor.attack`, `compressor.release`, `compressor.bands`, `compressor.low.upper_threshold`, `compressor.mid.upper_ratio`, `compressor.high.gain` (+12 band keys) | PARTIAL | limit mode, crossovers G73 |
| Convolve [S p.164] | 08 §1.2 | none | GAP | G79 |
| Delay [S p.165] | 08 §1.2 | `delay.on`, `delay.mix`, `delay.feedback`, `delay.time`, `delay.time2`, `delay.sync`, `delay.division`, `delay.division2`, `delay.style`, `delay.filter_cutoff`, `delay.filter_spread` | PARTIAL | Tap->Delay, HQ, scalar offset G74 |
| Distortion [S p.167] | 08 §1.2 | `distortion.on`, `distortion.type`, `distortion.drive`, `distortion.mix`, `distortion.filter_order`, `distortion.filter_cutoff`, `distortion.filter_resonance`, `distortion.filter_blend` | PARTIAL | X-Shaper, Tube / Diode, key track G75 |
| Equalizer [S p.169] | 08 §1.2 | `eq.on`, `eq.low.mode`, `eq.low.cutoff`, `eq.low.gain`, `eq.low.resonance`, `eq.mid.gain`, `eq.high.gain` (+6 keys) | HAVE | 3-band superset of Serum's 2-band; LEVEL G72 |
| Filter (FX) [S p.170] | 08 §1.2 | `fxfilter.on`, `fxfilter.model`, `fxfilter.style`, `fxfilter.cutoff`, `fxfilter.resonance`, `fxfilter.drive`, `fxfilter.blend`, `fxfilter.mix`, `fxfilter.keytrack` (+6 keys) | PARTIAL | Clean Mode, pan, level G54, G55, G72 |
| Flanger [S p.171] | 08 §1.2 | `flanger.on`, `flanger.mix`, `flanger.feedback`, `flanger.rate`, `flanger.sync`, `flanger.division`, `flanger.depth`, `flanger.center`, `flanger.phase_offset` | HAVE | LEVEL G72 |
| Hyper / Dimension [S p.173] | 08 §1.2 | none | GAP | G80 |
| Phaser [S p.174] | 08 §1.2 | `phaser.on`, `phaser.mix`, `phaser.feedback`, `phaser.rate`, `phaser.sync`, `phaser.division`, `phaser.depth`, `phaser.center`, `phaser.blend`, `phaser.phase_offset` | PARTIAL | DEPTH 2 G76 |
| Reverb [S p.175] | 08 §1.2 | `reverb.on`, `reverb.mix`, `reverb.size`, `reverb.decay`, `reverb.predelay`, `reverb.pre_low_cut`, `reverb.pre_high_cut`, `reverb.low_shelf_cutoff`, `reverb.low_shelf_gain`, `reverb.high_shelf_cutoff`, `reverb.high_shelf_gain`, `reverb.chorus_amount`, `reverb.chorus_rate` | PARTIAL | Plate / Hall / Vintage / Nitrous types G77 |
| Splitter L/H, L/M/H, MS [S p.177-181] | 08 §1.2 | none | GAP | G82 |
| Utility [S p.182] | 08 §1.2 | none | GAP | G81 |

### 1.6 Modulation (pp.183-214)

| Serum function | Our design (file §) | Our keys | Status | Note |
|----------------|---------------------|----------|--------|------|
| Using envelopes: configuring (A, H, D, S, R, curves, delay) [S p.183-186] | 05 §1.1 | `env1.delay`, `env1.attack`, `env1.hold`, `env1.decay`, `env1.sustain`, `env1.release`, `env1.attack_curve`, `env1.decay_curve`, `env1.release_curve` (x6) | HAVE | 6 envelopes vs Serum 4 |
| Envelope zoom lock / grid [S p.184-185] | 05 §1.1 | none | PARTIAL | time grid HAVE (UI); beat grid + MS / BPM units G59 |
| Envelope Legato Inverted [S p.185] | 05 §1.1 | none | GAP | G60 |
| Modifying envelopes (drag points) [S p.186] | 05 §2.1, §3 | `env1.attack`, `env1.attack_curve` ... | HAVE | |
| Assigning an envelope to a control [S p.187] | 06 §2.1 | `mod01.source` .. `mod64.source` (`env1`..`env6`), `mod01.destination` | HAVE | tap source chip, drag knob |
| Using LFOs: configuring, LFO tiles (1-10) [S p.188-189] | 05 §1.2 | `lfo1.shape` .. `lfo8.shape`, `lfo1.rate` | PARTIAL | 8 LFOs + 4 random; LFO 9-10 G61 |
| Drawing an LFO graph (points, tools, brush, grid, presets) [S p.189-192] | 05 §1.2, §2.4 | `lfo1.shape` (12 built-ins only) | GAP | G62 |
| LFO type: Normal, S&H, Chaos Lorenz [S p.191] | 05 §1.2, §1.3 | `lfo1.shape`, `rlfo1.style` | HAVE | |
| LFO type: Path, Chaos Rossler [S p.191] | 05 §1.2 | none | GAP | G63 (Path is `[S unclear]`) |
| LFO mode FREE / RETRIG / ENVELOPE [S p.191] | 05 §1.2 | `lfo1.sync_type` | HAVE | |
| LFO MONO, DIRECTION, HOST, swing, 10x, phase snap [S p.191-193] | 05 §1.2 | none | GAP | G64 |
| LFO BPM / HZ, TRIP / DOT, rate [S p.192] | 05 §1.2 | `lfo1.rate`, `lfo1.sync`, `lfo1.division` | HAVE | |
| LFO RISE, DELAY, SMOOTH, PHASE [S p.192-193] | 05 §1.2 | `lfo1.fade_in`, `lfo1.delay`, `lfo1.smooth`, `lfo1.smooth_time`, `lfo1.phase` | HAVE | |
| LFO editor on a large canvas [S p.193] | 05 §2.2, §2.4 | none | GAP | G62 |
| Assigning an LFO to a control [S p.194] | 06 §2.1 | `mod01.source` (`lfo1`..`lfo8`, `rlfo1`..`rlfo4`) | HAVE | |
| Setting modulator depth, negative depth [S p.195-196] | 06 §2.1, §2.3 | `mod01.amount`, `mod01.bipolar` | HAVE | purple ring |
| Copy wavetable shape to LFO / LFO shape to wavetable [S p.197-198] | 03 §1.1, 05 §1.2 | none | GAP | G52 |
| Modulating LFO points: menus, drag and drop [S p.198-200] | 05 §1.2, §2.2 | none | GAP | G65 |
| Using context menus with controls [S p.200-201] | 06 §2.2 | `mod01.*`, `mod01.bypass` | PARTIAL | Aux Source, custom curve G68, G69 |
| Velocity settings (VELO graph) [S p.202-203] | 05 §1.4, 06 §2.4 | source `velocity`, `master.velocity_track` | PARTIAL | custom curve graph G66 |
| Note settings (NOTE graph) [S p.204-205] | 05 §1.4, 06 §2.4 | source `note` | PARTIAL | custom curve graph G66 |
| Using macros 1-8, assigning, swapping [S p.206-208] | 01 §1.2, 06 §1 | `macro.1`, `macro.2`, `macro.3`, `macro.4`, `mod01.source` (`macro1`..`macro4`) | PARTIAL | macros 5-8 G01 |
| Oscillators and filters as mod sources [S p.208] | 06 §1 | none | GAP | G29 |
| Mod matrix: exploring (source, CRV, amount, polarity, stereo, dest) [S p.209-211] | 06 §2.3 | `mod01.source`, `mod01.destination`, `mod01.amount`, `mod01.power`, `mod01.bipolar`, `mod01.stereo`, `mod01.bypass` (x64) | PARTIAL | AUX SOURCE G68, editable curve + RISE / FALL G69 |
| Moving / bypassing / removing modulations in the matrix [S p.212-213] | 06 §2.3 | `mod01.bypass`, `mod01.source` | HAVE | |
| Matrix operations (sort, lock, create vibrato) [S p.214] | 06 §1, §2.3 | `mod01.*`, `master.tune` | HAVE | lock = UI |

### 1.7 Voicing, Clips, Arpeggiator, Keyboard (pp.215-273)

| Serum function | Our design (file §) | Our keys | Status | Note |
|----------------|---------------------|----------|--------|------|
| Voicing: Mono [S p.216] | 07 §2.1 | `master.polyphony` (= 1) | HAVE | |
| Voicing: Legato [S p.216] | 07 §2.1 | `master.legato` | PARTIAL | mono legato HAVE; poly legato G13 |
| Voicing: Poly (voice count, same-note limit) [S p.216] | 07 §2.1 | `master.polyphony` | PARTIAL | same-note switch, steal priority G12 |
| Portamento: Porta, Curve, Always, Scaled [S p.217-218] | 07 §2.1 | `master.portamento.time`, `master.portamento.slope`, `master.portamento.always`, `master.portamento.scaled` | HAVE | |
| Clips: CLIP module, global parameters, clip banks, piano roll, grid, zoom, fold, clip length, settings, managing, triggering, recording, MIDI out, routing, configuring, macros [S p.219-243] | - | - | EXCLUDED | excluded: Kick (grid / sequencer) |
| Arpeggiator: module, global parameters, banks, patterns, graph editor, transpose, playback, retrigger, velocity, managing, triggering, MIDI out, configuring [S p.244-266] | - | - | EXCLUDED | excluded: Kick |
| Keyboard: Transpose [S p.267] | 01 §1.3 | `master.transpose` | HAVE | strip +-24, sheet full range |
| Keyboard: Key and Scale [S p.268] | 01 §1.3, 07 §1 | none | GAP | G05 |
| Keyboard: Swing [S p.269] | 01 §1.3 | none | EXCLUDED | grid swing = Kick; LFO swing G16; X5 |
| Keyboard: Oscillator Mapping (key / velocity ranges, FOLD, WARP) [S p.269-273] | 01 §1.3, 07 §1 | none | GAP | G06 |

### 1.8 Wavetable editor (pp.274-303)

| Serum function | Our design (file §) | Our keys | Status | Note |
|----------------|---------------------|----------|--------|------|
| Using the wavetable editor, thumbnails, select / reorder [S p.274-275] | 03 §1.2, §2.2 | none | GAP | G42 |
| Drawing tools (flat, sine, slopes, curves, half sines, interpolate, nudge, noise, mirror), grid [S p.276-277] | 03 §1.2, §2.3 | none | GAP | G41 |
| FFT area: draw amplitude / phase [S p.278] | 03 §1.2, §2.3 | none | GAP | G43 |
| FFT menu (clear, saw, randomize, fade, shift, odd / even) [S p.279-280] | 03 §1.2 | none | GAP | G43 |
| Managing frames: copy / paste, insert / remove, sort [S p.281-283] | 03 §1.2, §2.4 | none | GAP | G44 |
| Formula parser (field in the editor) [S p.283-284] | 03 §1.3, §2.5 | none | GAP | G48 |
| Menu commands: import a wavetable [S p.285] | 03 §1.4, §2.6 | none | GAP | G50 |
| Menu commands: export (single / all menus) [S p.285-288] | 03 §1.3 | none | GAP | G49; process ops = G45 |
| Morph menu [S p.289] | 03 §1.3 | none | GAP | G46 |
| Saving wavetables [S p.290] | 03 §1.1, §1.3 | none | GAP | G40 |
| Importing audio as wavetables: multi-cycle, advanced, text overrides, single-cycle, import menu, specific sound [S p.291-299] | 03 §1.4 | none | GAP | G50 (text overrides n/a on mobile) |
| Importing an image file as a wavetable [S p.300-301] | 03 §1.4 | none | GAP | G50 |
| Animate WT POS with an LFO after import [S p.301] | 03 §1.4 | `lfo1.shape` (Saw Up), `osc1.frame`, `mod01.destination` | HAVE | |
| Embedding wavetables when saving a preset, Missing Files dialog [S p.302-303] | 03 §1.4, 01 §1.1 | none | GAP | G40, X3 |
| Mod wheel scans WT POS in the editor [S p.315] | 03 §1.1 | `osc1.frame` | HAVE | preview only |

### 1.9 Formula parser (pp.304-311)

| Serum function | Our design (file §) | Our keys | Status | Note |
|----------------|---------------------|----------|--------|------|
| Basic functions [S p.304] | 03 §1.3 | none | GAP | G48 |
| Built-in binary operators [S p.305] | 03 §1.3 | none | GAP | G48 (`?:`, `%` grammar `[S unclear]`) |
| Constants and variables [S p.306] | 03 §1.3 | none | GAP | G48 |
| Formula presets menu, saving own formulas, manual file management [S p.307-308] | 03 §1.3 | none | GAP | G48 (file management n/a on mobile) |
| Formula parser examples [S p.309-311] | 03 §2.5 | none | GAP | G48 (example list in sheet) |

### 1.10 Global settings (pp.312-322)

| Serum function | Our design (file §) | Our keys | Status | Note |
|----------------|---------------------|----------|--------|------|
| Using the Global module [S p.312] | 07 §2 | none | HAVE | sub-tab layout |
| Preferences: tooltips, double-click, wheel, shortcuts, default view [S p.313-315] | 01 §1.4, 07 §1 | none | HAVE | UI settings file |
| Preferences: MPE, limit mod depth on drop, mod wheel to WT pos, silence on host stop, MIDI map from preset [S p.314-315] | 07 §1, §2.4 | none | PARTIAL | UI prefs HAVE; MPE G10 |
| Voice control: loading preset, new configuration, configuring [S p.316-317] | 07 §2.3 | none | GAP | G17 |
| Voice control: randomization, scaling, saving [S p.318-319] | 07 §2.3 | none | GAP | G17 |
| Setting the quality (Draft / High / Ultra) [S p.319] | 07 §2.2 | none (quality tier, `IEngine::setQuality`) | PARTIAL | 4x + smoothing switch G18 |
| Tuning: concert pitch, tuning file, MTS-ESP, lock [S p.320-321] | 07 §2.4 | none | GAP | G11 |
| Checking the build version and date [S p.322] | 07 §2.4 | none | HAVE | About sheet |

### 1.11 Appendices (pp.323-354)

| Serum function | Our design (file §) | Our keys | Status | Note |
|----------------|---------------------|----------|--------|------|
| A: Main menu (Init, Init LFOs / Mods, Revert, Save as Default, Render OSC Warp, Resample, MIDI map, tuning, MPE) [S p.323-326] | 01 §1.1, 07 §1 | all keys (init) | PARTIAL | Save as Default G02; Render / Resample G53, G03; MIDI/MPE G07, G10 |
| B: Presets browser (navigating, loading, previewing, searching, metadata, categories, ratings, operations, creating / exporting a pack) [S p.327-339] | - | - | EXCLUDED | excluded: Kick |
| C: Preferences file: default artist name [S p.340] | 01 §1.1 | none | HAVE | artist in Prefs |
| C: Preset changes via MIDI controller / notes; oscillator preset changes via MIDI [S p.341-343] | - | - | EXCLUDED | preset selection = Kick / host |
| D: Serum file structure [S p.344-345] | 03 §1.3, 11 §1.1 | none | EXCLUDED | not a function; our formats in ARCHITECTURE 5.2 |
| E: Creating wavetables (good wavetable, ordering, interpolation, from scratch) [S p.346-347] | 03 §2.6 | none | GAP | G50 help text in import sheet |
| F: Optimizing Serum (CPU, unison) [S p.348] | 07 §2.2 | `osc1.unison.voices` | HAVE | quality tiers cap voices / unison |
| G: Keyboard shortcuts [S p.349-353] | 01 §1.4 | any key | HAVE | every shortcut has a touch alternative |
| Additional credits [S p.354] | - | - | EXCLUDED | not a function |

## 2. Vital matrix

### 2.1 Vital manual (43 pages)

| Vital function | Our design (file §) | Our keys | Status | Note |
|----------------|---------------------|----------|--------|------|
| 1.1 Sizing [V p.3] | 01 §1.1 | none | HAVE | UI scale |
| 1.2 General navigation [V p.3] | 01 §1.2 | none | HAVE | page tabs |
| 1.3 Presets: browse / save / open external / init [V p.3-4] | 01 §1.1, 11 §2.1 | none | PARTIAL | browser = EXCLUDED Kick; `.vital` open = 11 (new code, GAP G84) |
| 1.4 Parameter adjustments (drag, fine, numeric, reset) [V p.4] | 01 §1.4 | any key | HAVE | |
| 2.0 Voice: 3 oscillators + sampler + 2 filters in parallel [V p.5] | 02, 04 | `osc1.on`, `filter1.on` | PARTIAL | sampler GAP (G20) |
| 2.1 Oscillator: activator, level, pan [V p.6] | 02 §1.1 | `osc1.on`, `osc1.level`, `osc1.pan` | HAVE | |
| 2.1.a Pitch settings, transpose snap [V p.6-7] | 02 §1.1 | `osc1.transpose`, `osc1.tune` | PARTIAL | snap keyboard G05/G26 |
| 2.1.b Routing options (F1, F2, both, FX, direct) [V p.7] | 02 §1.1 | `osc1.destination` | HAVE | |
| 2.1.c Wavetable view 2D / 3D / spectral, position slider [V p.7-8] | 03 §1.1 | `osc1.wavetable`, `osc1.frame` | HAVE | |
| 2.1.c.i Wavetable editor [V p.8] | 03 §1.2-1.3 | none | GAP | G40-G50; contradicts ARCHITECTURE 3.3 (X1) |
| 2.1.d Unison and phase settings [V p.8-9] | 02 §1.2 | `osc1.unison.voices`, `osc1.unison.detune`, `osc1.unison.stack`, `osc1.unison.spread`, `osc1.unison.blend`, `osc1.phase`, `osc1.random_phase` | HAVE | |
| 2.1.e Left mod knob: spectral morph algorithms (Vocode, Formant Scale, Harmonic Stretch, Inharmonic Stretch, Smear, ...) [V p.9] | 02 §1.2 | `osc1.morph.type`, `osc1.morph.amount`, `osc1.morph.spread` | HAVE | |
| 2.1.e Right mod knob: Sync, Bend, PWM, Quantize, Formant, Squeeze, FM, RM [V p.10] | 02 §1.2 | `osc1.warp.type`, `osc1.warp.amount`, `osc1.warp.phase`, `osc1.warp.spread` | HAVE | FM from sampler dropped (G36) |
| 2.2 Sampler: level, pan, routing, pitch, playback options [V p.10-12] | 02 §1.4, §1.8 | `noise.on`, `noise.level`, `noise.pan`, `noise.destination` | PARTIAL | only built-in white noise; custom sample G20, G38 |
| 2.3 Filters: types, frequency response, cutoff, resonance, drive, mix [V p.13-14] | 04 §1.1 | `filter1.model`, `filter1.style`, `filter1.cutoff`, `filter1.resonance`, `filter1.drive`, `filter1.mix` | HAVE | |
| 2.3 Filter blend and keytrack [V p.13-14] | 04 §1.1 | `filter1.blend`, `filter1.keytrack` | HAVE | |
| 2.3 Filter routing: oscillators / sampler / other filter [V p.13] | 04 §1.1 | `filter.routing`, `osc1.destination` | HAVE | sampler GAP (G20) |
| 2.3 Formant filter [V p.15] | 04 §1.2 | `filter1.formant.x`, `filter1.formant.y`, `filter1.formant.resonance`, `filter1.formant.spread`, `filter1.formant.transpose` | HAVE | |
| 2.3 Comb and phaser filter [V p.15] | 04 §1.2 | `filter1.comb_offset`, `filter1.style`, `filter1.blend` | HAVE | |
| 3.0 Modulation overview, cyan modulation display, hover audition [V p.16] | 06 §2.1 | `mod01.source`, `mod01.destination`, `mod01.amount` | HAVE | purple ring |
| 3.1 Envelopes (delay, attack, hold, decay, sustain, release, curves) [V p.17-18] | 05 §1.1 | `env1.delay`, `env1.attack`, `env1.hold`, `env1.decay`, `env1.sustain`, `env1.release`, `env1.attack_curve` | HAVE | env1 mapped to amp by default |
| 3.2 LFOs: shape chooser, brush, grid, phase, mode, frequency, smooth, delay, stereo [V p.18-20] | 05 §1.2 | `lfo1.shape`, `lfo1.rate`, `lfo1.sync`, `lfo1.sync_type`, `lfo1.phase`, `lfo1.smooth`, `lfo1.fade_in`, `lfo1.delay`, `lfo1.stereo`, `lfo1.keytrack_transpose` | PARTIAL | shape drawing / brush G62 |
| 3.3 Random LFOs: Perlin, S&H, Sine interp., Lorenz [V p.21-22] | 05 §1.3 | `rlfo1.style`, `rlfo1.rate`, `rlfo1.sync`, `rlfo1.division`, `rlfo1.sync_type`, `rlfo1.stereo`, `rlfo1.keytrack_transpose`, `rlfo1.keytrack_tune` | HAVE | |
| 3.4 Other sources: Note, Velocity, Random [V p.22-23] | 06 §1 | source `note`, `velocity`, `random` | HAVE | |
| 3.4 Other sources: Lift, Slide, Pressure, Stereo, Oct Note [V p.22-23] | 06 §1 | source `aftertouch` (pressure) | PARTIAL | Lift, Slide, Oct Note, Stereo = G67 (X4) |
| 3.5 Macros 1-4 [V p.23] | 01 §1.2 | `macro.1`, `macro.2`, `macro.3`, `macro.4` | HAVE | |
| 3.6 Keyboard, pitch wheel, mod wheel [V p.23] | 01 §1.3 | `master.bend_range`, source `pitch_wheel`, `mod_wheel` | HAVE | |
| 4.1 Modulation list (bypass, bipolar, stereo, amount, power) [V p.24-25] | 06 §2.3 | `mod01.bypass`, `mod01.bipolar`, `mod01.stereo`, `mod01.amount`, `mod01.power` | HAVE | 64 slots |
| 4.2 Modulation remap curve (editable) [V p.25-26] | 06 §2.3 | `mod01.power` | PARTIAL | free-form curve G69 |
| 5.0 Effects chain, 9 effects, reorder [V p.27] | 08 §1.1 | `fx.order.1` .. `fx.order.9` | HAVE | |
| 5.1 Chorus [V p.27] | 08 §1.2 | `chorus.on`, `chorus.mix`, `chorus.voices`, `chorus.rate`, `chorus.depth` | HAVE | |
| 5.2 Compressor (multiband, upward / downward) [V p.28-29] | 08 §1.2 | `compressor.bands`, `compressor.low.upper_threshold`, `compressor.attack`, `compressor.release` | HAVE | |
| 5.3 Delay [V p.30-31] | 08 §1.2 | `delay.time`, `delay.style`, `delay.feedback`, `delay.filter_cutoff`, `delay.mix` | HAVE | |
| 5.4 Distortion [V p.32-33] | 08 §1.2 | `distortion.type`, `distortion.drive`, `distortion.filter_order`, `distortion.mix` | HAVE | |
| 5.5 EQ [V p.34] | 08 §1.2 | `eq.low.mode`, `eq.mid.gain`, `eq.high.cutoff` | HAVE | |
| 5.6 Filter (FX) [V p.34] | 08 §1.2 | `fxfilter.model`, `fxfilter.cutoff`, `fxfilter.mix` | HAVE | |
| 5.7 Flanger [V p.35] | 08 §1.2 | `flanger.rate`, `flanger.center`, `flanger.feedback` | HAVE | |
| 5.8 Phaser [V p.36-37] | 08 §1.2 | `phaser.rate`, `phaser.blend`, `phaser.center` | HAVE | |
| 5.9 Reverb [V p.37-38] | 08 §1.2 | `reverb.size`, `reverb.decay`, `reverb.predelay`, `reverb.mix` | HAVE | tier Low bypasses reverb, flanger (08 Q4) |
| 6.0 Global: voices, bend, legato, portamento, velocity track [V p.39] | 07 §1 | `master.polyphony`, `master.bend_range`, `master.legato`, `master.portamento.time`, `master.velocity_track` | HAVE | |
| 7.1 Oscillator advanced: note tracking [V p.40] | 02 §1.1 | none | GAP | G27 |
| 7.1 Hi-resolution wavetable [V p.40] | 02 §1.2 | none | GAP | G32 |
| 7.1 Unison stack options, detune range, dry / wet [V p.40-41] | 02 §1.2 | `osc1.unison.stack`, `osc1.unison.blend` | PARTIAL | detune range G32 |
| 7.1 Spectral unison switch [V p.40] | 02 §1.2 | none (always on) | HAVE | fixed on |
| 7.2 Global advanced: oscilloscope, spectrum analyzer [V p.42] | 01 §1.1 | none | HAVE | UI |
| 7.2 Global oversampling [V p.42] | 07 §2.2 | none (quality tier) | PARTIAL | 1x / 2x by tier; no free switch |
| 7.2 Frequency units option [V p.42] | 01 §1.4 | none | HAVE | UI |
| 7.2 Voice settings: note priority, voice override [V p.43] | 07 §2.1 | none | GAP | G12 |
| 7.2 Tuning (Lo-Fi / scale files) [V p.43] | 07 §2.4 | none | GAP | G11 |

### 2.2 Vital engine features (docs/PARAMETERS.md, core/engine-vital/README.md, 11 §1.2.11)

| Vital engine feature | Our design (file §) | Our keys | Status | Note |
|----------------------|---------------------|----------|--------|------|
| Wavetable group / component / keyframe model, 10 component types (Wave Source, Line Source, Audio File Source, Shepard Tone, Phase Shift, Wave Window, Frequency Filter, Slew Limiter, Wave Folder, Wave Warp) | 03 §1.2-1.3, §2.7; 11 §1.3.1 | none | GAP | G42, G47; Shepard Tone source not offered (deprecated); 11 needs a renderer (11 Q1) |
| Wavetable editor: frame list, draw, spectrum, morph, import | 03 §2.2-2.6; ARCHITECTURE 3.3 | none | GAP | G40-G50; X1 |
| LFO shape drawing: line generator (points, powers, smooth) | 05 §1.2, §2.4 | `lfo1.shape` (12 built-ins) | GAP | G62; 11 imports custom shapes through C2 |
| Custom wavetable in a preset (3 per `.vital` file) | 11 §1.3.1 | `osc1.wavetable` (13 built-ins) | GAP | G24 |
| Custom LFO shapes in a preset (8 per `.vital` file) | 11 §1.3.2 | `lfo1.shape` | GAP | G62, G24 |
| Sub oscillator (`sub_*`, 8 controls) | 02 §1.7; 11 §1.2.11 | none | GAP | G23 |
| Sample oscillator (`sample_*`, 7 controls beyond noise) | 02 §1.4; 11 §1.3.3 | none | GAP | G20 |
| Built-in white-noise sample | 02 §1.8 | `noise.on`, `noise.level`, `noise.pan`, `noise.destination` | HAVE | |
| Per-oscillator `detune_range`, `midi_track`, `transpose_quantize`, `smooth_interpolation`, `view_2d`, `spectral_unison` | 02 §1.1-1.2 | none | GAP | G27, G32, G26, G30; view_2d is UI |
| Voice priority / override / stealing | 07 §1 | none | GAP | G12 |
| Oversampling, stereo mode / routing, MPE, bypass, bpm | 07 §2.2, §2.4 | none | PARTIAL | tier / host business (PARAMETERS "Not exposed"); MPE G10 |
| Tuning tables (`settings.tuning`) | 07 §2.4; 11 Q8 | none | GAP | G11 |
| Mod sources lift, slide, stereo, note-in-octave | 06 §1 | none | GAP | G67 |
| Line-map curve per modulation slot | 06 §1 | `mod01.power` | PARTIAL | G69 |
| Effect-chain order, 9 effects, tier bypass | 08 §1.1 | `fx.order.1` .. `fx.order.9` | HAVE | |
| `.vital` preset import (parse, map 730 + 150 controls, report, batch) | 11 §1, §2 | all 880 keys are targets | GAP | G84: new code; keys exist |
| Stand-alone `.vitaltable` / `.vitallfo` import | 11 §1.3, A.2 | none | GAP | G84 |
| Export as `.vital` | 11 C.3 | none | EXCLUDED | decided: not a product feature (11 Q10) |
| 64 Vital controls without a one-to-one key | 11 §1.2.11 | none | GAP | classified there (dropped / approximated / ignored), see G12, G20, G23, G27, G32 |

## 3. Consolidated GAP list

84 unique GAPs (IDs G01-G84), de-duplicated across 01-09 and 11. "Files" = where the file marks the GAP. "Engine would need" = one-line core requirement.

### 3.1 Shell, voice, MIDI, global

| ID | GAP | Files | Engine would need |
|----|-----|-------|-------------------|
| G01 | Macros 5-8 | 01 §1.2, 06 §1 | four more macro keys + 4 mod-source choices |
| G02 | Save as Default Preset | 01 §1.1 | user default-state slot loaded by Init (BUILT, 15-shell-features.md 3) |
| G03 | Drag-out / Share last note as audio; Resample to wavetable; offline render | 01 §1.1, 03 §1.1, 07 §1 | offline render API for the last note / one bar |
| G04 | Embed a sample in the preset | 01 §1.1 | sample oscillator + ContentPack samples |
| G05 | Key + Scale quantizer, Vital Global Snap, transpose-snap keyboard | 01 §1.3, 02 §1.1, 07 §1 | note-filter stage in `IEngine` (or Kick MIDI pipeline) |
| G06 | Oscillator mapping: key / velocity ranges, FOLD, WARP flag | 01 §1.3, 02 §1.1, 07 §1 | per-oscillator range state + voice gating |
| G07 | MIDI Learn / CC map | 01 §1.4 | CC-to-parameter table in core (07 treats it as shell layer, X2) (BUILT, 15-shell-features.md 2) |
| G08 | Lock parameter / lock module (survive preset load) | 01 §1.4, 08 §1.1 | locked-key set in the state loader (BUILT for modules and keys, 15-shell-features.md 4) |
| G09 | Sustain pedal (CC64) | 07 §1 | `Event::Type::sustainPedal` (engine-vital already implements it) |
| G10 | MPE / per-note expression (bend, slide, pressure, poly AT) | 07 §1, 06 §1 | note-expression events through CLAP + engine voices |
| G11 | Tuning: A4 reference, `.scl` / `.kbm` / `.tun`, MTS-ESP | 07 §1, 11 §1.2.11 | expose `sbvital::Tuning` on `IEngine` |
| G12 | Voice steal priority, voice override, same-note poly switch | 07 §1 | priority policy parameter (engine steals oldest) |
| G13 | Poly (paraphonic) legato | 07 §1 | legato state per voice group |
| G14 | Separate bend up / down; MPE bend range 1-96 | 07 §1 | two bend ranges |
| G15 | Global stereo spread / rotate | 07 §1 | master stereo stage |
| G16 | Swing for tempo-synced LFOs | 05 §1.2, 07 §1 | swing parameter in LFO / delay clocks |
| G17 | Per-voice control (random pan / detune / cutoff / envs, 1-8 step sequence, env / LFO time scaling) | 07 §2.3 | per-voice variation block |
| G18 | 4x oversampling, disable parameter smoothing | 07 §2.2 | tier 4x + smoothing bypass |

### 3.2 Oscillators and mixer

| ID | GAP | Files | Engine would need |
|----|-----|-------|-------------------|
| G19 | Multisample oscillator (SFZ, zones, envelope, vel track, timbre) | 02 §1.3 | new oscillator type + ContentPack instruments |
| G20 | Sample oscillator (also Vital sampler): load, markers, slicing, loops, scan, unison start / span, pitch | 02 §1.4 | sample oscillator (engine-vital has one, not exposed) + ContentPack samples |
| G21 | Granular oscillator | 02 §1.5 | new DSP |
| G22 | Spectral oscillator | 02 §1.6 | new DSP |
| G23 | Sub oscillator | 02 §1.7, 04 §1.1 | expose engine-vital `sub_*` (8 controls) |
| G24 | User / ContentPack wavetable selection | 02 §1.2, 03 §1.1, 11 §1.3.1 | content-id parameter or state field + `setUserWavetable` (11 C1) |
| G25 | CRS continuous coarse pitch | 02 §1.1 | non-stepped transpose destination |
| G26 | Pitch modes Harmonics / Ratio (SRC osc) / Step (MTS-ESP) | 02 §1.1 | pitch-mode state per oscillator |
| G27 | Pitch tracking toggle, pitch-bend tracking when off | 02 §1.1 | expose engine `midi_track` |
| G28 | Routing knob blend Filter 1 / 2, destination None, Bus 1 / 2 sends | 02 §1.1, 04 §1.1 | continuous routing parameter, None choice, bus sends |
| G29 | Oscillator / noise / sub / filter outputs as mod or warp source (FM, PD, AM, RM from Noise, Sub, Filters) | 02 §1.2, 04 §1.1, 06 §1 | audio-rate source taps in the matrix + warp sources |
| G30 | WT POS range up to 256 frames, per-osc smooth interpolation switch | 02 §1.2, 03 §1.1 | variable frame count; interpolation parameter |
| G31 | Phase Legato, phase memory | 02 §1.2 | phase policy per oscillator |
| G32 | Unison: 5 detune MODE laws, RANGE, stack repeat 1-3x, WARP 2 spread, hi-res wavetable | 02 §1.2 | extra unison parameters |
| G33 | Two free warp slots + Swap Warps | 02 §1.2 | second warp slot with full mode list |
| G34 | Warp modes: Var fader, Bend + / -, Asym x3, Flip, Mirror, Remap 1-4, Odd / Even | 02 §1.2 | new warp kernels |
| G35 | Warp Filter group (LPF / HPF) and Distortion group (14 modes) | 02 §1.2 | new warp kernels |
| G36 | Warp FM variants (Thru-Zero, Exp, Linear), FM from sampler | 02 §1.2 | FM law choices; sample osc (G20) |
| G37 | Warp PD and AM modes | 02 §1.2 | new warp kernels |
| G38 | Noise: colours, STEREO / FILTER knobs, own sample, one-shot / loop, start, random, pitch, fine | 02 §1.8 | noise sample player (G20) |
| G39 | Mixer: ENV 1 toggle, Bus 1 / 2 channels, DIRECT level, filter outputs (Main / Direct / None / buses), per-bus FX bypass | 02 §1.9, 04 §1.1 | bus graph in the voice mixer |

### 3.3 Wavetable (editor, import)

| ID | GAP | Files | Engine would need |
|----|-----|-------|-------------------|
| G40 | Wavetable name / modified state, Save / Load / Embed table, stored as content (`.sbwt`) | 03 §1.1, §1.3, 01 §1.1 | Phase 4 content tools + preset `content` section (11 C3, C4) |
| G41 | Drawing tools, grid | 03 §1.2 | `WavetableGroup` model API for edits |
| G42 | Keyframe / component model (Wave Source, Line Source), thumbnails, per-component interpolation | 03 §1.2 | core wavetable group (ARCHITECTURE 3.3) |
| G43 | FFT area + FFT menu | 03 §1.2 | spectral edit commands |
| G44 | Frame ops: copy / paste, insert / remove, sort | 03 §1.2 | frame commands |
| G45 | Process menus SINGLE / ALL (normalize, flip, fade, filter, blur, PWM, set spectra, ...) | 03 §1.3 | process commands |
| G46 | Morph menu (crossfade, spectral) | 03 §1.3 | morph commands |
| G47 | Vital modifiers: phase shift, wave window, frequency filter, slew limiter, wave folder, wave warp | 03 §1.3 | component renderers |
| G48 | Formula parser | 03 §1.3 | expression evaluator (grammar `[S unclear]`) |
| G49 | Export (.256, WAV 16 / 32 bit, single-cycle) | 03 §1.3 | writer functions |
| G50 | Import: audio multi-cycle modes, FFT / vocoded, Vital styles, stereo, single-cycle, PNG image | 03 §1.4 | importer functions |
| G51 | Switch OSC type on import / sample-to-wavetable | 02 §1.4-1.6, 03 §1.4 | needs G19-G22 |
| G52 | Copy wavetable frame to LFO, LFO shape to wavetable | 03 §1.1, 05 §1.2, 06 §1 | shape transfer API (needs G62) |
| G53 | Render OSC Warp (frame to 256 frames across WARP) | 03 §1.1 | offline warp render |

### 3.4 Filters

| ID | GAP | Files | Engine would need |
|----|-----|-------|-------------------|
| G54 | Drive Clean Mode (filters and FX filter) | 04 §1.1, 08 §1.2 | pre / post gain mode flag (**built, WTREMAP B16: `filterN.clean_mode`, 13 §4.15**) |
| G55 | Filter pan (L / R cutoff offset) and level | 04 §1.1, 08 §1.2 | two parameters per filter |
| G56 | Keytrack follows oscillator pitch incl. portamento | 04 §1.1 | key-follow source choice |
| G57 | Drawable PZ SVF filter | 04 §1.1 | new filter + pole / zero state |
| G58 | About 35 of 60 Serum filter types (dual SVF multi, Ring Mod, SampHold, Combs, Allpasses, Reverb, Add Bass, New Wsp, DJ Mixer, EMS Ladder, Scream, ...) | 04 §1.3 | new filter models |

### 3.5 Envelopes, LFOs, modulation

| ID | GAP | Files | Engine would need |
|----|-----|-------|-------------------|
| G59 | Envelope time units MS / BPM, beat grid | 05 §1.1, 06 §1 | tempo-synced envelope times |
| G60 | Envelope Legato Inverted | 05 §1.1, 06 §1 | per-envelope retrigger flag |
| G61 | LFO 9-10 | 05 §1.2 | two more `lfoN` blocks |
| G62 | Free-form LFO shapes: points, curves, tools, brush, grid, user shape presets, large editor | 05 §1.2, 06 §1, 11 §1.3.2 | point-list state; custom `lfoN.shape` choice (11 C2) |
| G63 | LFO type Path (undefined in manual), Chaos Rossler | 05 §1.2, 06 §1 | new generators |
| G64 | LFO MONO / poly, Direction (forward / reverse / ping-pong), HOST anchor, 10x range, phase snap | 05 §1.2, 06 §1 | LFO state flags |
| G65 | LFO point modulation (X / Y via LFO busses, drag source onto point) | 05 §1.2, 06 §1 | modulatable shape points |
| G66 | VELO / NOTE custom curve graphs (+ legato option) | 05 §1.4, 06 §1 | curve state for the two sources |
| G67 | Mod sources Lift, Slide, Oct Note, Pressure / Poly AT, Stereo, Note-On Rand 2 / discrete, Active Voices, Voice Index, Voice Mod | 05 §1.4, 06 §1, 11 §1.2.7 | new `modSources` choices + events |
| G68 | Matrix AUX SOURCE (+ INV, AUX CRV, OUTPUT) | 06 §1, §2.2 | per-slot aux fields |
| G69 | Per-slot editable remap curve, RISE / FALL slew | 01 §1.4, 06 §1 | curve state per slot (**wavetable position part built, WTREMAP B16: `oscN.remap.*`, `content.remaps`, 13 §4.14; the mod matrix part is open, 14 row 18**) |

### 3.6 Effects

| ID | GAP | Files | Engine would need |
|----|-----|-------|-------------------|
| G70 | Parallel FX racks MAIN / BUS 1 / BUS 2 | 08 §1.1 | bus routing (see G39) |
| G71 | Dynamic module instancing (add / remove / duplicate) | 08 §1.1 | variable effect graph |
| G72 | Per-module output LEVEL trim | 08 §1.1, §1.2 | one parameter per effect (**built, WTREMAP B16: `<fx>.level` of the eight Vital effects, 13 §4.14**) |
| G73 | Compressor limit mode, adjustable crossovers | 08 §1.2 | limiter + crossover parameters |
| G74 | Delay Tap->Delay, HQ, scalar offset | 08 §1.2 | delay modes |
| G75 | Distortion X-Shaper (custom graphs), Tube / Diode / Rectify, filter key track | 08 §1.2 | waveshaper types + graph state |
| G76 | Phaser DEPTH 2 | 08 §1.2 | second depth parameter |
| G77 | Reverb algorithms Plate / Hall / Vintage / Nitrous + room modes | 08 §1.2 | reverb type parameter |
| G78 | Bode frequency shifter | 08 §1.2 | new effect |
| G79 | Convolve (impulse responses) | 08 §1.2 | new effect + IR content |
| G80 | Hyper / Dimension | 08 §1.2 | new effect |
| G81 | Utility | 08 §1.2 | new effect |
| G82 | Splitters L/H, L/M/H, MS (3 modules) | 08 §1.2 | band / mid-side splitter graph |

### 3.7 Kick integration and Vital import

| ID | GAP | Files | Engine would need |
|----|-----|-------|-------------------|
| G83 | `clap.remote-controls` extension (8-knob pages) | 09 §1 | implement extension in `shell-clap` / `shell-juce` (Phase 5) |
| G84 | `.vital` / `.vitaltable` / `.vitallfo` import: parser, wavetable renderer, report, entry points, batch | 11 §1.0, A.1 | new code `core/src/vital_import/` + core changes C1-C5 (A.4) |

Not counted as GAP (HAVE or UI-only): oscilloscope, A/B compare, tooltips, undo / redo, layouts.

## 4. Reachability of all 880 keys

Verified by the owner and re-run 2026-11-06 (the snippet in [09 §6.1](09-kick-integration.md) was run again while writing this file with the same result): 880 keys in `docs/params.json`; 426 on the 61 Kick pages of [09 §5](09-kick-integration.md); 454 via More sheets (env release curve, lfo smooth + stereo + keytrack: 46 keys) and the `mod05`..`mod64` matrix (408 keys, of which `mod05..16.amount` are on pages 60-61); 0 unknown keys. Snippet: see [09 §6.1](09-kick-integration.md) (not repeated here). Breakdown table: [09 §6.2](09-kick-integration.md). Per-area key groups: §6 of each area file.

## 5. Cross-file contradictions and owner questions

### 5.1 Contradictions found

| # | Contradiction | Where | Resolution needed |
|---|---------------|-------|-------------------|
| X1 | Wavetable editor: ARCHITECTURE 3.3 says "Mobile/MPC: no editor"; 03 designs a full mobile editor; PARAMETERS "Not exposed" says no editor or import yet (Phase 5, UI) | docs/ARCHITECTURE.md 3.3 vs 03 §2.2, §7 Q1 | owner decision (Q7); update ARCHITECTURE 3.3 |
| X2 | MIDI Learn: 01 marks it GAP ("no CC to parameter map in core"); 07 marks it HAVE (shell / Kick layer) | 01 §1.4 vs 07 §1 | decide the layer, then align both |
| X3 | Embed wavetable in preset: 01 and 02 say HAVE; 03 says GAP (no UI path); 11 A.4 says wavetables live in the pack and are never embedded (1 MiB limit) | 01 §1.1, 02 §1.1 vs 03 §1.1, 11 A.4 | owner decision (Q8) |
| X4 | Vital "Stereo" mod source: 06 says HAVE via the per-slot `modNN.stereo` flag; PARAMETERS and 11 §1.2.7 say the source is not exposed and the connection is dropped on import | 06 §1 vs PARAMETERS "Not exposed", 11 §1.2.7 | the flag is a slot property, not the source; fix 06 |
| X5 | Swing: 01 treats it as excluded (Kick, grid only); 05 / 06 / 07 list LFO swing and Global Swing as GAP | 01 §1.3 vs 05 §1.2, 07 §1 | split: grid swing = Kick, LFO swing = G16 |
| X6 | 11 §7 Q2 says "core changes C1 to C4"; Annex A.4 lists C1-C5 | 11 §7 vs A.4 | fix the question text |
| X7 | 00-BRIEF says the Vital manual has 8 pages; the text has 43 pages and files cite up to `[V p.43]` | 00-BRIEF vs `_manuals/vital` | fix the brief |
| X8 | Kick remote pages: 02 §5 defines 3 pages per oscillator (Main / Unison / Types) with different knob sets than 09 §5 (`osc1_main`, `osc1_unison`, `osc1_shaping`) | 02 §5 vs 09 §5 | 09 §5 is the canonical 61-page set (verified by script); mark 01-08 §5 as proposals |
| X9 | Envelope / LFO / macro counts | 05, 06, 09, params.json | consistent: 6 env, 8 LFO + 4 random, 4 macros (Serum 4 / 10 / 8); no contradiction |
| X10 | Key counts of matrix slots | 06 (448), 09 (40 on pages + 408), 11 (448) | consistent |
| X11 | Master transpose range: strip +-24 (01) vs key range +-48 (07) | 01 §1.3, 07 §1 | documented in 01, not a contradiction |
| X12 | Built-in LFO shapes: 12 (engine README, 05, 06); wavetables: 13 | engine-vital README | consistent |

(X9-X12 are listed so a reader knows they were checked.)

### 5.2 Open owner questions (merged, deduplicated)

| # | Question | Origin |
|---|----------|--------|
| Q1 | Confirm Serum Clips (p.219) and Arpeggiator (p.244), plus Send-to-clip and grid swing, are excluded (Kick); no internal arp / clip player | 01 Q1, Q6; 02 Q5; 05 Q4; 06 Q1; 07 Q1; 08 Q1; 09 Q1 |
| Q2 | Are Key / Scale, Osc Mapping, macros 5-8 wanted on the roadmap (UI is designed for them)? Do Key / Scale and Swing belong to Kick's MIDI pipeline or to the core? Keep 4 macros or add four more macro keys? | 01 Q2; 06 Q2; 07 Q1 |
| Q3 | Double-tap default: reset (Vital) or numeric entry (Serum)? | 01 Q3 |
| Q4 | Is the A/B compare wanted in the first UI? | 01 Q4 |
| Q5 | MPC screen resolution and which hardware controls reach the plug-in UI (8 encoders per page assumed) | 01 Q5 |
| Q6 | Which engine delivers multisample / sample / granular / spectral / sub oscillators and the 14 distortion warps? Add the sample oscillator (also for Vital sample import)? | 02 Q1; 11 Q7 |
| Q7 | Mobile wavetable editor despite ARCHITECTURE 3.3: confirm, and confirm the data model (Vital group / component / keyframe + Serum frame operations) | 03 Q1 |
| Q8 | User wavetables: content-id parameter vs state-only; how to hand a table to the engine; embedded tables vs the 1 MiB state limit | 02 Q2; 03 Q2, Q3; 11 Q2 |
| Q9 | Keep two free warp slots (Serum) or Vital's fixed warp + morph families? | 02 Q3 |
| Q10 | Wavetable export formats: all five or WAV 16 / 32 bit and single-cycle only? | 03 Q4 |
| Q11 | Formula parser: full Serum grammar (incl. `q` FFT mode) or a subset? | 03 Q5 |
| Q12 | Image import, Render OSC Warp and Resample wanted? | 03 Q6 |
| Q13 | Which DSP delivers the ~35 missing Serum filter types? The "nearest" mappings need an A/B listening pass before the picker names are final | 04 Q1, Q2 |
| Q14 | Keytrack: follow oscillator pitch (Serum) or MIDI note (Vital)? | 04 Q3 |
| Q15 | Per-filter pan and level wanted? | 04 Q4 |
| Q16 | Continuous oscillator blend Filter 1 / 2, Bus 1 / 2, DIRECT level and parallel FX racks: engine roadmap, or keep one serial 9-slot chain? | 02 Q4; 04 Q5; 08 Q3 |
| Q17 | Which LFO / envelope GAPs are wanted (free-form shapes, point modulation, Velocity / Note curves, Rossler, MONO, Direction, HOST, LFO 9-10, beat units)? Is the shape stored as a point list in the preset? Implement or drop LFO type Path (not described in the manual)? Keep 6 env / 8 + 4 LFO? | 05 Q1, Q2, Q3 |
| Q18 | Matrix aux source: add `modNN.aux_*` keys or rely on the macro-as-destination workaround? | 06 Q3 |
| Q19 | Expose Lift, Slide, Oct Note, Poly Aftertouch as mod sources (engine-vital has the processors)? | 06 Q4 |
| Q20 | Free-form per-slot remap curves and VELO / NOTE graphs in state JSON, or is `modNN.power` enough? | 06 Q5 |
| Q21 | LFO point modulation and oscillator / filter audio-rate mod sources: confirm they stay GAP | 06 Q6 |
| Q22 | Add a sustain-pedal event (`Event::Type::sustainPedal`)? | 07 Q2 |
| Q23 | Expose tuning (A4, `.scl` / `.kbm` / `.tun`, MTS-ESP) or keep 12-TET at 440 Hz? (the Vital import drops `settings.tuning`) | 07 Q3; 11 Q8 |
| Q24 | Wire MPE (per-note bend, pressure, slide) through CLAP note expression into the engines? | 07 Q4 |
| Q25 | Per-voice control and oscillator key / velocity ranges: keep as GAP or schedule for Phase 5? | 07 Q5 |
| Q26 | Stay with the 9 Vital effects for v1.0, or add Serum-only modules (Hyper / Dimension, Utility, Bode, Convolve, splitters)? | 08 Q2 |
| Q27 | `Tier::low` bypasses reverb and flanger: add a "Force enable reverb" toggle? | 08 Q4 |
| Q28 | Where does `kick-pages.json` live: generated by `synthyboy_paramdump` or committed as `docs/kick-pages.json`? | 09 Q2 |
| Q29 | Update `scripts/gen-factory-presets.py` so every factory preset has the category macro wiring (09 §2.3)? | 09 Q3 |
| Q30 | MPC Q-Link page cycler: skip pages of disabled modules by default? | 09 Q4 |
| Q31 | Vital import: build the own clean-room wavetable renderer for all 10 component types (largest work item) and approve the procedure (A.5) | 11 Q1 |
| Q32 | Approve core changes C1-C5 (append `oscN.wavetable` / `lfoN.shape` choices, `.sbpreset` sections, regenerate params.json; keys and ids unchanged) | 11 Q2 |
| Q33 | Licence sign-off for shipping the 730-row Vital control-name table | 11 Q3 |
| Q34 | Frame-position semantics for Vital tables with fewer than 257 frames (settle by A/B fixture `custom_wavetable`) | 11 Q4 |
| Q35 | Support `.vitalbank` (zip) for batch import? | 11 Q5 |
| Q36 | Does engine-own define `modNN.amount` as a fraction of the destination range like Vital (1:1 import), or is a per-destination rescale table needed? | 11 Q6 |
| Q37 | Vital style mapping of `Percussion` and `Sequence` (proposal, `[unverified]`) | 11 Q9 |
| Q38 | Confirm: no export as `.vital` | 11 Q10 |
| Q39 | Kick integration on mobile: CLAP custom extension vs linking `synthyboy_core` directly | 11 Q11; 09 §1 |
| Q40 | Keep `tests/vital-presets/` out of git and CI (add to `.gitignore`)? | 11 Q12 |
