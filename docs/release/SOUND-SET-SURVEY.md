# SynthyBoy Factory Sound Set Survey & Engine Gap Analysis

Quantitative survey of the 112 factory presets in `content/presets/` against the 2637 engine parameters in `docs/params.json`.

---

## 1. Quantitative Findings

### 1.a. Preset Count and Parameter Difference Distribution

The factory sound set contains 112 presets across 6 categories. Out of 2637 total engine parameters declared in `docs/params.json`, presets vary between only 34 and 64 parameters from factory defaults (mean: 44.4, median: 44.0). Over 97.5% of engine parameters remain untouched at their default values in every preset.

| Category | Preset Count | Percentage | Min Diffs | Max Diffs | Mean Diffs | Median Diffs | Std Dev |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Overall** | **112** | **100.0%** | **34** | **64** | **44.4** | **44.0** | **6.3** |
| Bass | 23 | 20.5% | 40 | 56 | 46.3 | 45.0 | 4.1 |
| FX | 11 | 9.8% | 50 | 64 | 54.5 | 54.0 | 4.3 |
| Keys | 17 | 15.2% | 39 | 56 | 43.4 | 41.0 | 4.3 |
| Lead | 22 | 19.6% | 37 | 51 | 40.4 | 39.0 | 3.7 |
| Pad | 23 | 20.5% | 42 | 58 | 47.2 | 47.0 | 4.6 |
| Pluck | 16 | 14.3% | 34 | 47 | 37.6 | 37.0 | 3.5 |

#### Distribution Breakdown

- **30–34 differing parameters**: 1 preset (0.9%) — `Electric Mute Pluck` (34 diffs)
- **35–39 differing parameters**: 26 presets (23.2%)
- **40–44 differing parameters**: 35 presets (31.3%)
- **45–49 differing parameters**: 30 presets (26.8%)
- **50–54 differing parameters**: 12 presets (10.7%)
- **55–59 differing parameters**: 6 presets (5.4%)
- **60–64 differing parameters**: 2 presets (1.8%) — `Bode Shifter Drone` (64 diffs), `Alien Communication` (62 diffs)

---

### 1.b. Similarity & Template Families

Pairwise parameter comparison across all 6216 preset pairs (`112 * 111 / 2`) demonstrates that the sound set is built from a small collection of rigid templates.

- **Total Pairs Evaluated**: 6216
- **Difference Range**: 4 to 71 parameters
- **Mean Pair Distance**: 37.7 parameters
- **Pairs with <= 6 differences**: 143 pairs
- **Pairs with <= 10 differences**: 452 pairs

#### 30 Closest Preset Pairs

| Rank | Preset A | Cat A | Preset B | Cat B | Diffs | Differing Parameters |
| :---: | :--- | :--- | :--- | :--- | :---: | :--- |
| 1 | Comb Metal Bass | Bass | Moog Ladder Bass | Bass | **4** | `filter1.cutoff`, `filter1.model`, `filter1.resonance`, `master.volume` |
| 2 | Digital Glass Tap | Pluck | Harmonic Tine Pluck | Pluck | **4** | `env1.decay`, `filter1.cutoff`, `master.volume`, `osc1.wavetable` |
| 3 | Digital Glass Tap | Pluck | Spruce Guitar | Pluck | **4** | `env1.decay`, `filter1.cutoff`, `master.volume`, `osc1.wavetable` |
| 4 | Harmonic Sine Bass | Bass | Odd Harmonics Bass | Bass | **4** | `filter1.cutoff`, `filter1.resonance`, `master.volume`, `osc1.wavetable` |
| 5 | Harmonic Sine Bass | Bass | Solid Sub | Bass | **4** | `filter1.cutoff`, `filter1.resonance`, `master.volume`, `osc1.wavetable` |
| 6 | Harmonic Tine Pluck | Pluck | Spruce Guitar | Pluck | **4** | `env1.decay`, `filter1.cutoff`, `master.volume`, `osc1.wavetable` |
| 7 | Odd Harmonics Bass | Bass | Solid Sub | Bass | **4** | `filter1.cutoff`, `filter1.resonance`, `master.volume`, `osc1.wavetable` |
| 8 | Radiant Pulse Pad | Pad | Subtle Tape Drift | Pad | **4** | `env1.attack`, `env1.release`, `lfo1.rate`, `osc1.wavetable` |
| 9 | 808 Boom | Bass | Solid Sub | Bass | **5** | `env1.decay`, `env1.release`, `env1.sustain`, `filter1.cutoff`, `master.volume` |
| 10 | Acid Saw Reso | Bass | Moog Ladder Bass | Bass | **5** | `env2.decay`, `env2.sustain`, `filter1.cutoff`, `filter1.resonance`, `master.volume` |
| 11 | Acoustic Style Pluck | Pluck | Marimba Synth | Pluck | **5** | `env1.attack`, `env1.release`, `filter1.cutoff`, `master.volume`, `osc1.wavetable` |
| 12 | B3 Drawbar Organ | Keys | Harpsichord Bar | Keys | **5** | `env1.release`, `env1.sustain`, `filter1.cutoff`, `master.volume`, `osc1.wavetable` |
| 13 | Bell Mallet Pluck | Pluck | Trance Short Pluck | Pluck | **5** | `delay.mix`, `env1.attack`, `filter1.cutoff`, `master.volume`, `osc1.wavetable` |
| 14 | Chime Drop | Pluck | Digital Glass Tap | Pluck | **5** | `filter1.cutoff`, `master.volume`, `osc1.wavetable`, `reverb.decay`, `reverb.mix` |
| 15 | Chime Drop | Pluck | Harmonic Tine Pluck | Pluck | **5** | `filter1.cutoff`, `master.volume`, `osc1.wavetable`, `reverb.decay`, `reverb.mix` |
| 16 | Chime Drop | Pluck | Harp Drops | Pluck | **5** | `delay.mix`, `filter1.cutoff`, `master.volume`, `osc1.wavetable`, `reverb.decay` |
| 17 | Chime Drop | Pluck | Spruce Guitar | Pluck | **5** | `filter1.cutoff`, `master.volume`, `osc1.wavetable`, `reverb.decay`, `reverb.mix` |
| 18 | Comb Metal Bass | Bass | Formant Vowel Bass | Bass | **5** | `filter1.cutoff`, `filter1.model`, `filter1.resonance`, `master.volume`, `osc1.wavetable` |
| 19 | Comb Metal Bass | Bass | Harmonic Sine Bass | Bass | **5** | `filter1.cutoff`, `filter1.model`, `filter1.resonance`, `master.volume`, `osc1.wavetable` |
| 20 | Comb Metal Bass | Bass | Odd Harmonics Bass | Bass | **5** | `filter1.cutoff`, `filter1.model`, `filter1.resonance`, `master.volume`, `osc1.wavetable` |
| 21 | Comb Metal Bass | Bass | Solid Sub | Bass | **5** | `filter1.cutoff`, `filter1.model`, `filter1.resonance`, `master.volume`, `osc1.wavetable` |
| 22 | Comb Metal Bass | Bass | Warm Analog Bass | Bass | **5** | `filter1.cutoff`, `filter1.model`, `filter1.resonance`, `master.volume`, `osc1.wavetable` |
| 23 | Digital Glass Tap | Pluck | Formant Pop Pluck | Pluck | **5** | `filter1.cutoff`, `filter1.model`, `filter1.resonance`, `master.volume`, `osc1.wavetable` |
| 24 | Digital Glass Tap | Pluck | Marimba Synth | Pluck | **5** | `env1.attack`, `filter1.cutoff`, `master.volume`, `osc1.wavetable`, `reverb.mix` |
| 25 | Digital Glass Tap | Pluck | Rubber Bass Pluck | Pluck | **5** | `filter1.cutoff`, `filter1.resonance`, `master.volume`, `osc1.wavetable`, `reverb.mix` |
| 26 | Digital Glass Tap | Pluck | Trance Short Pluck | Pluck | **5** | `delay.mix`, `filter1.cutoff`, `master.volume`, `osc1.wavetable`, `reverb.mix` |
| 27 | Diode Saturated Bass | Bass | Solid Sub | Bass | **5** | `filter1.cutoff`, `filter1.drive`, `filter1.model`, `master.volume`, `osc1.wavetable` |
| 28 | Diode Saturated Bass | Bass | Warm Analog Bass | Bass | **5** | `filter1.cutoff`, `filter1.drive`, `filter1.model`, `filter1.resonance`, `master.volume` |
| 29 | Formant Pop Pluck | Pluck | Harmonic Tine Pluck | Pluck | **5** | `filter1.cutoff`, `filter1.model`, `filter1.resonance`, `master.volume`, `osc1.wavetable` |
| 30 | Formant Pop Pluck | Pluck | Spruce Guitar | Pluck | **5** | `filter1.cutoff`, `filter1.model`, `filter1.resonance`, `master.volume`, `osc1.wavetable` |

#### Template Family Clusters at N <= 15 Differences

At a distance threshold of 15 parameters, the presets form exactly 7 distinct multi-preset clusters corresponding directly to category templates:

| Cluster | Member Count | Categories | Template Description & Sample Members |
| :---: | :---: | :--- | :--- |
| **Cluster 1** | **21** | Pad | Pad template: `Cathedral Choir`, `Diffusor Cloud Pad`, `Velvet Strings`, `Infinite Horizon`, `Lush Analog Pad` |
| **Cluster 2** | **19** | Bass | Bass template: `808 Boom`, `Warm Analog Bass`, `Sync Growl Bass`, `Solid Sub`, `Sub Duo Octave`, `Moog Ladder Bass` |
| **Cluster 3** | **19** | Lead | Lead template: `Bell Chime Lead`, `Warm Vintage Lead`, `Screaming Sync Lead`, `Dual Octave Hero`, `Vocaloid Lead` |
| **Cluster 4** | **17** | Keys | Keys template (all 17 presets): `Accordion Reed`, `Warm Toy Piano`, `Vibraphone Bars`, `Muted Honky Tonk`, `B3 Drawbar Organ` |
| **Cluster 5** | **15** | Pluck | Pluck template: `Acoustic Style Pluck`, `Trance Short Pluck`, `Spruce Guitar`, `Rubber Bass Pluck`, `Pizzicato Strings` |
| **Cluster 6** | **7** | FX | Resonant FX template: `Alien Communication`, `Sub Drop Boom`, `Sonar Ping`, `Sci-Fi Laser Sweep`, `Metal Gong Strike` |
| **Cluster 7** | **3** | FX | Noise/Atmosphere FX template: `Noise Riser`, `Vinyl Dust Crackle`, `Space Wind Storm` |
| Singletons | 11 | Pad (2), Bass (4), Lead (3), Pluck (1), FX (1) | `EMS Squelch Bass`, `Scream Filter Bass`, `MPE Pressure Lead`, `Solo Square Lead`, `Aux Keyscale Pluck`, `Bode Shifter Drone` |

#### Template Family Clusters at N <= 30 Differences

At a distance threshold of 30 parameters, **96 presets (85.7% of the entire library)** collapse into a single giant template component spanning Bass, Keys, Lead, Pad, and Pluck. Only FX presets and two specialized basses remain outside this macro-cluster:

| Cluster | Member Count | Categories | Composition |
| :---: | :---: | :--- | :--- |
| **Cluster 1** | **96** | Bass (21), Keys (17), Lead (22), Pad (21), Pluck (15) | Unified core template family sharing identical oscillator, filter, and macro wiring |
| **Cluster 2** | **11** | FX (11) | Entire FX category |
| **Cluster 3** | **2** | Bass (2) | `EMS Squelch Bass`, `Scream Filter Bass` |
| Singletons | 3 | Pad (2), Pluck (1) | `Frozen Aurora`, `Comb Reso Swell`, `Aux Keyscale Pluck` |

---

### 1.c. Feature Use & Engine Utilization

#### Core Module Activation (112 Presets Total)

| Module | Active Presets | Inactive Presets | Utilization Rate |
| :--- | :---: | :---: | :---: |
| **Oscillator A (`osc1.on`)** | 111 | 1 | 99.1% |
| **Oscillator B (`osc2.on`)** | 13 | 99 | 11.6% |
| **Oscillator C (`osc3.on`)** | 0 | 112 | **0.0%** |
| **Sub Oscillator (`sub.on`)** | 0 | 112 | **0.0%** |
| **Noise Generator (`noise.on`)** | 5 | 107 | 4.5% |
| **Filter 1 (`filter1.on`)** | 112 | 0 | 100.0% |
| **Filter 2 (`filter2.on`)** | 0 | 112 | **0.0%** |
| **Mixer Bus 1 (`bus1.*`)** | 0 | 112 | **0.0%** |
| **Mixer Bus 2 (`bus2.*`)** | 0 | 112 | **0.0%** |

#### Effect Module Activation

| Effect | Active Presets | Utilization Rate | Status in Sound Set |
| :--- | :---: | :---: | :--- |
| Reverb (`reverb.on`) | 89 | 79.5% | Heavily used |
| Chorus (`chorus.on`) | 67 | 59.8% | Widely used |
| Delay (`delay.on`) | 51 | 45.5% | Moderately used |
| Distortion (`distortion.on`) | 40 | 35.7% | Moderately used |
| Phaser (`phaser.on`) | 17 | 15.2% | Lightly used |
| Utility (`utility.on`) | 2 | 1.8% | Rare (2 presets) |
| Bode (`bode.on`) | 1 | 0.9% | Rare (1 preset: `Bode Shifter Drone`) |
| Hyper/Dimension (`hyperdim.on`) | 1 | 0.9% | Rare (1 preset: `Hyper Dimension Pad`) |
| **Compressor (`compressor.on`)** | **0** | **0.0%** | **Completely Unused** |
| **EQ (`eq.on`)** | **0** | **0.0%** | **Completely Unused** |
| **Flanger (`flanger.on`)** | **0** | **0.0%** | **Completely Unused** |
| **Convolve (`convolve.on`)** | **0** | **0.0%** | **Completely Unused** |

*Note: All 16 modular FX slots (`fxi01.type` .. `fxi16.type`) are set to 0 (`None`) across all 112 presets.*

#### Filter Models Utilization

Out of 27 filter models available in the engine:
- **Digital (model 3)**: 90 presets (80.4%)
- **Formant (model 5)**: 5 presets
- **Comb (model 6)**: 4 presets
- **Ladder (model 2)**: 3 presets
- **Analog (model 0)**: 2 presets
- **Dirty (model 1)**: 2 presets
- **Diode (model 4)**: 2 presets
- **Diffusor (model 19)**: 1 preset
- **EMS Ladder (model 20)**: 1 preset
- **Phaser (model 7)**: 1 preset
- **Scream (model 16)**: 1 preset
- **16 UNUSED Models (0 presets)**: `Multi SVF`, `Slope`, `EQ 6 dB`, `Ring Mod`, `Sample Hold`, `Smear`, `French LP`, `Add Bass`, `New Wsp`, `DJ Mixer`, `PZ SVF`, `Normal`, `Multi Morph`, `Flanges`, `Misc`, `New`.

#### Oscillator Warp Types Utilization

Out of 99 warp choices available in the engine:
- **None (warp 0)**: 77 presets (68.8%)
- **Sync (warp 1)**: 8 presets
- **Formant (warp 2)**: 6 presets
- **Quantize (warp 3)**: 5 presets
- **Bend (warp 4)**: 5 presets
- **94 UNUSED Warp Types**: Over 90% of warp modes are never selected. Secondary warps (`osc1.warp2.type`, `osc2.warp2.type`) are 0 in all 112 presets.

#### Unison Voices & Stack Modes

- **1 Voice (No Unison)**: 93 presets (83.0%)
- **2 Voices**: 1 preset
- **3 Voices**: 4 presets
- **4 Voices**: 4 presets
- **5 Voices**: 5 presets
- **6 Voices**: 2 presets
- **7 Voices**: 2 presets
- **8 Voices**: 1 preset
- **9 to 16 Voices**: **0 presets**
- **Unison Stack Modes**: 100 presets use standard Unison (stack 0); Center Drop 12 (stack 1) in 5 presets, Octaves (stack 3) in 7 presets. Power Chord, Two Power Chords, Major Chord, Minor Chord, Harmonic Series, and Odd Harmonics are never used.

#### Modulation Matrix Sources & Destinations

- **Total Active Routes**: 1271
- **Modulation Sources**: 14 used / **43 unused** (out of 57)
- **Modulation Destinations**: 33 used / **1215 unused** (out of 1248)

| Source | ID | Key | Routes Across Set | Notes |
| :--- | :---: | :--- | :---: | :--- |
| Macro 3 | 21 | `macro3` | 279 | Primary envelope/rate macro |
| Macro 2 | 20 | `macro2` | 268 | Primary timbre/warp macro |
| Macro 4 | 22 | `macro4` | 255 | Primary FX/reverb macro |
| Macro 1 | 19 | `macro1` | 235 | Primary filter cutoff macro |
| Macro 6 | 30 | `macro6` | 89 | Resonance macro |
| LFO 1 | 7 | `lfo1` | 76 | Filter and pitch modulation |
| Macro 7 | 31 | `macro7` | 26 | Osc 2 detune macro |
| Macro 5 | 29 | `macro5` | 21 | Osc 1 spread macro |
| Random LFO 1 | 15 | `rlfo1` | 11 | Drift/detune |
| Macro 8 | 32 | `macro8` | 5 | Noise level macro |
| Env 2 | 2 | `env2` | 2 | Filter envelope |
| Poly Aftertouch | 38 | `poly_aftertouch` | 2 | Present only in `MPE Pressure Lead` |
| Env 1 | 1 | `env1` | 1 | Amp envelope route |
| Slide | 36 | `slide` | 1 | Present only in `MPE Pressure Lead` |

**43 Completely Unused Modulation Sources**:
`Env 3`, `Env 4`, `Env 5`, `Env 6`, `LFO 2`, `LFO 3`, `LFO 4`, `LFO 5`, `LFO 6`, `LFO 7`, `LFO 8`, `LFO 9`, `LFO 10`, `Random LFO 2`, `Random LFO 3`, `Random LFO 4`, `Velocity`, `Release Velocity`, `Note (Keytrack)`, `Oct Note`, `Channel Aftertouch`, `Pitch Wheel`, `Mod Wheel`, `Random per Note`, `Lift`, `Expression X`, `Stereo`, `Voice Index`, `Voice Mod 1`, `Voice Mod 2`, `Oscillator A`, `Oscillator B`, `Oscillator C`, `Noise`, `Sub`, `Filter 1`, `Filter 2`, `Active Voices`, `Note-On Alt 1`, `Note-On Alt 2`, `Note-On Rand 2`, `Note-On Rand Discrete`, `Fixed`.

---

### 1.d. Performance Response

The factory sound set lacks real-time performance responsiveness:

| Performance Source | Presets Routing Source | Percentage | Details |
| :--- | :---: | :---: | :--- |
| **Velocity** | **0** | **0.0%** | Zero modulation routes from velocity in any preset |
| **Release Velocity** | **0** | **0.0%** | Completely unrouted |
| **Channel Aftertouch** | **0** | **0.0%** | Completely unrouted |
| **Polyphonic Aftertouch** | 1 | 0.9% | Routed in `MPE Pressure Lead` only |
| **Mod Wheel** | **0** | **0.0%** | Zero modulation routes from mod wheel in any preset |
| **Pitch Wheel** | **0** | **0.0%** | Zero modulation routes from pitch wheel |
| **Slide / Lift / Expression** | 1 | 0.9% | Slide routed in `MPE Pressure Lead` only |
| **Key Tracking (Pitch)** | **0** | **0.0%** | Zero routes from Note / Oct Note |
| **Random per Note** | **0** | **0.0%** | Zero routes from note random sources |
| **ANY Performance Route** | **1** | **0.9%** | **111 presets (99.1%) ignore all performance input** |

- **Master Velocity Tracking (`master.velocity_track`)**: 100% of presets (112 out of 112) have `master.velocity_track = 100.0`. Presets rely purely on fixed global velocity attenuation, with no timbral velocity sensitivity.

---

### 1.e. Housekeeping Integrity

| Check | Count | Findings |
| :--- | :---: | :--- |
| **Routes to Disabled Modules** | **79** | 63 routes to `filter2.cutoff` (`filter2.on=0`), 13 to `osc2.unison.detune` (`osc2.on=0`), 1 to `osc1.warp.amount` (`osc1.on=0` in `Noise Riser`), 1 to `noise.level` (`noise.on=0` in `Aux Keyscale Pluck`), 1 to `osc2.unison.detune` (`osc2.on=0` in `Laser Beam Lead`) |
| **Routes with Zero Amount** | 0 | None |
| **Macros with Targets but Zero Range** | 0 | None |
| **Identical Presets (Different Names)** | 0 | None |
| **Near-Identical Presets (<= 6 Differences)** | 143 pairs | Clustered clones differing only in master level, cutoff, and wavetable |

---

### 1.f. Levels Distribution

Master volume levels follow `scripts/factory-preset-levels.json`, normalized to -20 dBFS RMS chords via `scripts/tune-preset-levels.py`.

| Category | Presets | Min Level (dB) | Max Level (dB) | Mean Level (dB) | Median Level (dB) | Std Dev |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Overall** | **112** | **-27.8** | **+2.9** | **-12.0** | **-13.2** | **6.7** |
| Bass | 23 | -23.7 | +2.9 | -13.7 | -14.9 | 6.4 |
| FX | 11 | -20.8 | -10.1 | -15.2 | -16.4 | 3.2 |
| Keys | 17 | -22.1 | -3.8 | -13.1 | -14.0 | 5.6 |
| Lead | 22 | -27.8 | -2.0 | -14.7 | -14.8 | 5.6 |
| Pad | 23 | -20.3 | +2.8 | -11.5 | -12.5 | 6.7 |
| Pluck | 16 | -7.4 | +1.2 | -3.1 | -3.5 | 2.2 |

---

## 2. What the Engine Can Do That No Factory Preset Shows

1. **True Multi-Oscillator Stacking & Detuning**:
   - Parameters: `osc2.on`, `osc3.on`, `osc2.level`, `osc3.level`, `osc2.transpose`, `osc3.transpose`, `osc2.tune`, `osc3.tune`, `osc2.pan`, `osc3.pan`.
   - The engine provides three full-featured oscillators, yet Oscillator 3 is never turned on in any preset (`osc3.on = 0` in all 112), and Oscillator 2 is inactive in 88.4% of presets. Massive 3-oscillator supersaws, fifth-interval leads, and organ stacks are completely absent.

2. **Dedicated Low-Frequency Sub-Oscillator**:
   - Parameters: `sub.on`, `sub.waveform`, `sub.level`, `sub.octave`, `sub.tune`, `sub.mono`, `sub.destination`.
   - The engine offers a dedicated clean sub-oscillator with 6 waveforms and direct-out routing (`sub.destination = 4`). It is disabled (`sub.on = 0`) across all 112 factory presets.

3. **Advanced Synthesis Engines (Sample, Granular, Spectral, Multisample)**:
   - Parameters: `osc1.type`, `osc2.type`, `osc3.type` (choices: `Wavetable`, `Sample`, `Granular`, `Spectral`, `Multisample`), `oscX.gran.window_shape`, `oscX.gran.density_mode`, `oscX.gran.length_mode`, `oscX.sample.loop_mode`.
   - All 112 presets set `oscX.type = 0` (`Wavetable`). None of the Granular, Spectral, or Sample modes are ever showcased.
   - *Repository Content Audit*: Inspection of `content/` reveals only `content/presets/*.sbpreset`. There are NO sample files (`.wav`), multisamples, or external wavetables stored in `content/`. Therefore, Sample and Granular features must either load external user samples or derive grain buffers from built-in wavetable frames.

4. **16 Unused Filter Models & Dual-Filter Architecture**:
   - Parameters: `filter2.on`, `filter2.model`, `filter2.style`, `filter2.cutoff`, `filter2.resonance`, `filter.routing` (Parallel, Series 1->2, Series 2->1).
   - Filter 2 is never activated (`filter2.on = 0` everywhere), despite 63 presets actively modulating `filter2.cutoff`.
   - 16 filter models are never heard: Multi SVF (8), Slope (9), EQ 6 dB (10), Ring Mod (11), Sample Hold (12), Smear (13), French LP (14), Add Bass (15), New Wsp (17), DJ Mixer (18), PZ SVF (21), Normal (22), Multi Morph (23), Flanges (24), Misc (25), New (26).

5. **Built-in Dynamics, Equalization, Flanging & Convolution**:
   - Parameters: `compressor.on` (`compressor.threshold`, `compressor.ratio`, `compressor.attack`, `compressor.release`), `eq.on` (`eq.low_gain`, `eq.mid1_gain`, `eq.mid2_gain`, `eq.high_gain`), `flanger.on` (`flanger.rate`, `flanger.depth`, `flanger.feedback`), `convolve.on` (`convolve.impulse`, `convolve.mix`).
   - Four complete effect processors are completely dormant in all 112 presets.

6. **Auxiliary Submix Buses**:
   - Parameters: `bus1.level`, `bus2.level`, `bus1.destination`, `bus2.destination`, `oscX.bus1_send`, `filterX.bus1_send`.
   - Both mixer buses are completely unused.

7. **Expressive Real-Time Performance Control**:
   - Parameters: Mod Sources `velocity` (23), `release_velocity` (55), `aftertouch` (25), `mod_wheel` (27), `pitch_wheel` (26), `slide` (36), `lift` (35), `expression_x` (56), `note` (24), `random` (28).
   - 111 out of 112 presets possess zero routes from performance controllers to filter cutoff, volume, drive, or modulation rate.

8. **Auxiliary Modulation Generators (Envelopes 3–6, LFOs 2–10)**:
   - Parameters: `env3`..`env6`, `lfo2`..`lfo10`, `rlfo2`..`rlfo4`.
   - The engine features 6 envelopes, 10 LFOs, and 4 Random LFOs. The entire factory library only routes Envelope 1, Envelope 2, LFO 1, and Random LFO 1.

9. **Microtonal Scales, Tuning & MPE**:
   - Parameters: `master.mpe.on`, `master.mpe.xyz_macro`, `master.tuning.scale`, `master.scale.type` (20 musical scale modes).
   - 111 presets sit on standard 12-TET Chromatic with MPE disabled.

---

## 3. A Target List for a Stronger Sound Set

A sound designer's actionable target specification addressing the identified gaps without inventing engine features:

### 1. Unused Filter Model Showcase (32 New Presets)
- **Target**: Create 32 presets (8 Bass, 8 Lead, 8 Pad, 8 FX) featuring the 16 untouched filter models (`filter1.model` / `filter2.model`).
- **Required Parameters**:
  - `French LP` (model 14) & `Diode` (model 4) with high drive (`filter1.drive >= 15 dB`) for squelchy acid basses.
  - `Ring Mod` (model 11) & `Sample Hold` (model 12) for metallic clang and modular sci-fi FX.
  - `Multi SVF` (model 8) & `Multi Morph` (model 23) modulated by `lfo2.shape = 0` for vocal and talk-box pads.
  - `Add Bass` (model 15) for deep club and dubstep sub-basses.

### 2. Dual-Filter Architectures & Clean Up Defective Routes (24 Presets)
- **Target**: Fix the 63 presets with broken `filter2.cutoff` modulation by turning on `filter2.on = 1` and configuring meaningful routing (`filter.routing`).
- **Required Parameters**:
  - **Serial Split**: `filter2.on = 1`, `filter.routing = 1` (`Series 1 to 2`), where Filter 1 provides a 24 dB low-pass sweep and Filter 2 provides a steep high-pass cutoff (`filter2.style = 1`) to remove sub mud.
  - **Parallel Stereo Filter**: `filter2.on = 1`, `filter.routing = 0` (`Parallel`), with `osc1.destination = 0` (`Filter 1`, panned left) and `osc2.destination = 1` (`Filter 2`, panned right).

### 3. Integrated Dynamics, EQ & Flanger Deployment (40 Presets)
- **Target**: Activate the 4 dormant FX modules in at least 20 presets each:
  - **Compressor**: 25 punchy presets enabling `compressor.on = 1`, `compressor.threshold = -18.0`, `compressor.ratio = 4.0`, `compressor.attack = 15.0 ms`, `compressor.release = 80.0 ms`.
  - **EQ**: 30 presets enabling `eq.on = 1`, boosting presence (`eq.high_gain = 3.5 dB`, `eq.high_freq = 8000 Hz`) and cleaning low midrange mud (`eq.mid1_gain = -2.5 dB`, `eq.mid1_freq = 450 Hz`).
  - **Flanger**: 20 vintage presets enabling `flanger.on = 1`, `flanger.rate = 0.25 Hz`, `flanger.depth = 60%`, `flanger.feedback = 45%`.
  - **Convolve**: 15 ambient presets enabling `convolve.on = 1`, `convolve.mix = 35%`.

### 4. Triple-Oscillator & Dedicated Sub Layering (25 Presets)
- **Target**: Build full-spectrum layered sounds using the complete oscillator bank:
  - **3-Oscillator Supersaw & Octave Leads**: `osc1.on = 1`, `osc2.on = 1`, `osc3.on = 1`, with `osc1.unison.voices = 7`, `osc2.transpose = 7` (fifth), `osc3.transpose = 12` (octave).
  - **Clean Sub Bass Foundation**: 20 Bass presets setting `sub.on = 1`, `sub.waveform = 0` (Sine), `sub.level = 80%`, and routing directly to outputs `sub.destination = 4` (`Direct out`), bypassing voice filter distortion.

### 5. Evolving Spectral Morph Pads (15 Presets)
- **Target**: 15 atmospheric Pad presets utilizing spectral morphing:
  - **Required Parameters**: `osc1.morph.type` set to `Harmonic Stretch` (3), `Inharmonic Stretch` (4), `Smear` (5), or `Phase Disperse` (8).
  - Map `lfo2.shape = 0` (Sine) via `mod05.source = 8` (`LFO 2`) to `osc1.morph.amount` (`mod05.destination = 17`, `mod05.amount = 50.0`) for evolving wavetable textures.

### 6. Dynamic Velocity & Performance Mapping (100% of New/Revised Presets)
- **Target**: All presets in the library must respond to expressive touch:
  - **Velocity to Cutoff & Attack**: Map source 23 (`velocity`) to destination 1 (`filter1.cutoff`, amount +35) and destination 44 (`env1.attack`, amount -20) on all Plucks, Keys, and Basses.
  - **Aftertouch (Channel & Poly)**: Map source 25 (`aftertouch`) and source 38 (`poly_aftertouch`) to vibrato LFO depth and filter drive in Leads and Pads.
  - **Mod Wheel Mapping**: Map source 27 (`mod_wheel`) to LFO depth and secondary filter cutoff on all Leads and Pads.
  - **Key Tracking**: Map source 24 (`note`) to filter cutoff and decay times to emulate acoustic keyboard damping.

### 7. Wavetable Warp Showcase (20 Presets)
- **Target**: 20 presets showcasing underutilized warp modes:
  - Map `osc1.warp.type` to `Quantize` (3), `Formant` (2), `Bend` (4), `PWM`, and `Wavefolder`.
  - Introduce secondary warps via `osc1.warp2.type` and `osc1.warp2.amount`.

---
