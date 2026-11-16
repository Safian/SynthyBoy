# SynthyBoy Factory Sound Set Survey

Comprehensive quantitative analysis of the factory sound set across presets, parameters, similarity clusters, feature utilisation, performance response, housekeeping integrity, and output levels.

## 1. Executive Summary

- **Total Presets Analyzed**: 112
- **Engine Parameter Count**: 2637
- **Modulation Sources Available**: 57
- **Modulation Destinations Available**: 1248

| Category | Preset Count | Percentage |
| :--- | :---: | :---: |
| Bass | 23 | 20.5% |
| FX | 11 | 9.8% |
| Keys | 17 | 15.2% |
| Lead | 22 | 19.6% |
| Pad | 23 | 20.5% |
| Pluck | 16 | 14.3% |

## 2. Parameter Differences from Engine Defaults

Number of parameters per preset that differ from factory defaults (2637 total engine parameters).

| Scope | Count | Min Diffs | Max Diffs | Mean Diffs | Median Diffs | Std Dev |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Overall** | 112 | 34.0 | 64.0 | 44.4 | 44.0 | 6.3 |
| Bass | 23 | 40.0 | 56.0 | 46.3 | 45.0 | 4.1 |
| FX | 11 | 50.0 | 64.0 | 54.5 | 54.0 | 4.3 |
| Keys | 17 | 39.0 | 56.0 | 43.4 | 41.0 | 4.3 |
| Lead | 22 | 37.0 | 51.0 | 40.4 | 39.0 | 3.7 |
| Pad | 23 | 42.0 | 58.0 | 47.2 | 47.0 | 4.6 |
| Pluck | 16 | 34.0 | 47.0 | 37.6 | 37.0 | 3.5 |

## 3. Preset Similarity & Template Families

- **Total Preset Pairs**: 6216
- **Pair Difference Range**: 4.0 .. 71.0 parameters (Mean: 37.7, Median: 40.0)

### 30 Closest Preset Pairs

| Rank | Preset A | Category A | Preset B | Category B | Differing Params | Differing Parameter Keys |
| :---: | :--- | :--- | :--- | :--- | :---: | :--- |
| 1 | Comb Metal Bass | Bass | Moog Ladder Bass | Bass | **4** | `filter1.resonance, filter1.model, filter1.cutoff, master.volume` |
| 2 | Digital Glass Tap | Pluck | Harmonic Tine Pluck | Pluck | **4** | `osc1.wavetable, filter1.cutoff, master.volume, env1.decay` |
| 3 | Digital Glass Tap | Pluck | Spruce Guitar | Pluck | **4** | `osc1.wavetable, filter1.cutoff, master.volume, env1.decay` |
| 4 | Harmonic Sine Bass | Bass | Odd Harmonics Bass | Bass | **4** | `filter1.resonance, osc1.wavetable, filter1.cutoff, master.volume` |
| 5 | Harmonic Sine Bass | Bass | Solid Sub | Bass | **4** | `filter1.resonance, osc1.wavetable, filter1.cutoff, master.volume` |
| 6 | Harmonic Tine Pluck | Pluck | Spruce Guitar | Pluck | **4** | `osc1.wavetable, filter1.cutoff, master.volume, env1.decay` |
| 7 | Odd Harmonics Bass | Bass | Solid Sub | Bass | **4** | `filter1.resonance, osc1.wavetable, filter1.cutoff, master.volume` |
| 8 | Radiant Pulse Pad | Pad | Subtle Tape Drift | Pad | **4** | `env1.release, lfo1.rate, env1.attack, osc1.wavetable` |
| 9 | 808 Boom | Bass | Solid Sub | Bass | **5** | `env1.release, env1.sustain, filter1.cutoff, master.volume (+1 more)` |
| 10 | Acid Saw Reso | Bass | Moog Ladder Bass | Bass | **5** | `filter1.resonance, env2.decay, filter1.cutoff, master.volume (+1 more)` |
| 11 | Acoustic Style Pluck | Pluck | Marimba Synth | Pluck | **5** | `env1.release, osc1.wavetable, filter1.cutoff, master.volume (+1 more)` |
| 12 | B3 Drawbar Organ | Keys | Harpsichord Bar | Keys | **5** | `env1.release, env1.sustain, osc1.wavetable, filter1.cutoff (+1 more)` |
| 13 | Bell Mallet Pluck | Pluck | Trance Short Pluck | Pluck | **5** | `env1.attack, osc1.wavetable, filter1.cutoff, master.volume (+1 more)` |
| 14 | Chime Drop | Pluck | Digital Glass Tap | Pluck | **5** | `reverb.mix, osc1.wavetable, filter1.cutoff, master.volume (+1 more)` |
| 15 | Chime Drop | Pluck | Harmonic Tine Pluck | Pluck | **5** | `reverb.mix, osc1.wavetable, filter1.cutoff, master.volume (+1 more)` |
| 16 | Chime Drop | Pluck | Harp Drops | Pluck | **5** | `osc1.wavetable, delay.mix, filter1.cutoff, master.volume (+1 more)` |
| 17 | Chime Drop | Pluck | Spruce Guitar | Pluck | **5** | `reverb.mix, osc1.wavetable, filter1.cutoff, master.volume (+1 more)` |
| 18 | Comb Metal Bass | Bass | Formant Vowel Bass | Bass | **5** | `filter1.resonance, filter1.model, osc1.wavetable, filter1.cutoff (+1 more)` |
| 19 | Comb Metal Bass | Bass | Harmonic Sine Bass | Bass | **5** | `filter1.resonance, filter1.model, osc1.wavetable, filter1.cutoff (+1 more)` |
| 20 | Comb Metal Bass | Bass | Odd Harmonics Bass | Bass | **5** | `filter1.resonance, filter1.model, osc1.wavetable, filter1.cutoff (+1 more)` |
| 21 | Comb Metal Bass | Bass | Solid Sub | Bass | **5** | `filter1.resonance, filter1.model, osc1.wavetable, filter1.cutoff (+1 more)` |
| 22 | Comb Metal Bass | Bass | Warm Analog Bass | Bass | **5** | `filter1.resonance, filter1.model, osc1.wavetable, filter1.cutoff (+1 more)` |
| 23 | Digital Glass Tap | Pluck | Formant Pop Pluck | Pluck | **5** | `filter1.model, osc1.wavetable, filter1.cutoff, master.volume (+1 more)` |
| 24 | Digital Glass Tap | Pluck | Marimba Synth | Pluck | **5** | `env1.attack, osc1.wavetable, filter1.cutoff, master.volume (+1 more)` |
| 25 | Digital Glass Tap | Pluck | Rubber Bass Pluck | Pluck | **5** | `osc1.wavetable, filter1.cutoff, master.volume, env1.decay (+1 more)` |
| 26 | Digital Glass Tap | Pluck | Trance Short Pluck | Pluck | **5** | `osc1.wavetable, delay.mix, filter1.cutoff, master.volume (+1 more)` |
| 27 | Diode Bite Lead | Lead | Dirty Peak Lead | Lead | **5** | `filter1.resonance, filter1.model, osc1.wavetable, filter1.cutoff (+1 more)` |
| 28 | Diode Saturated Bass | Bass | Solid Sub | Bass | **5** | `filter1.model, osc1.wavetable, filter1.cutoff, master.volume (+1 more)` |
| 29 | Diode Saturated Bass | Bass | Warm Analog Bass | Bass | **5** | `filter1.resonance, filter1.model, filter1.cutoff, master.volume (+1 more)` |
| 30 | Formant Pop Pluck | Pluck | Harmonic Tine Pluck | Pluck | **5** | `filter1.model, osc1.wavetable, filter1.cutoff, master.volume (+1 more)` |

### Template Family Clusters (N <= 15 Differences)

| Cluster | Size | Categories | Sample Presets |
| :---: | :---: | :--- | :--- |
| 1 | 21 | Pad | Cathedral Choir, Comb Reso Swell, Dark Cinema Drone, Deep Sea Ambient (+17 more) |
| 2 | 19 | Bass | 808 Boom, Acid Saw Reso, Click Attack Bass, Comb Metal Bass (+15 more) |
| 3 | 19 | Lead | Bell Chime Lead, Chorused Saw Solo, Cosmic Drift Lead, Diode Bite Lead (+15 more) |
| 4 | 17 | Keys | Accordion Reed, B3 Drawbar Organ, Celesta Bells, Clavinet Funk (+13 more) |
| 5 | 15 | Pluck | Acoustic Style Pluck, Bell Mallet Pluck, Chime Drop, Comb Zither (+11 more) |
| 6 | 7 | FX | Alien Communication, Cosmic Teleport, Game Over Zap, Metal Gong Strike (+3 more) |
| 7 | 3 | FX | Noise Riser, Space Wind Storm, Vinyl Dust Crackle |
| 8 | 1 | Bass | Custom Shape Wobble |
| 9 | 1 | Bass | EMS Squelch Bass |
| 10 | 1 | Bass | Fat Reese |
| 11 | 1 | Bass | Scream Filter Bass |
| 12 | 1 | FX | Bode Shifter Drone |
| 13 | 1 | Lead | Hyper Saw Lead |
| 14 | 1 | Lead | MPE Pressure Lead |
| 15 | 1 | Lead | Twin Warp Lead |
| 16 | 1 | Pad | Lush Analog Pad |
| 17 | 1 | Pad | Tri-Band Split Texture |
| 18 | 1 | Pluck | Aux Keyscale Pluck |

### Template Family Clusters (N <= 30 Differences)

| Cluster | Size | Categories | Sample Presets |
| :---: | :---: | :--- | :--- |
| 1 | 96 | Bass, Keys, Lead, Pad, Pluck | 808 Boom, Acid Saw Reso, Click Attack Bass, Comb Metal Bass, Custom Shape Wobble (+91 more) |
| 2 | 11 | FX | Alien Communication, Bode Shifter Drone, Cosmic Teleport, Game Over Zap, Metal Gong Strike (+6 more) |
| 3 | 2 | Bass | EMS Squelch Bass, Scream Filter Bass |
| 4 | 1 | Lead | MPE Pressure Lead |
| 5 | 1 | Lead | Twin Warp Lead |
| 6 | 1 | Pluck | Aux Keyscale Pluck |

## 4. Feature Use & Engine Utilization

### Module Usage Across 112 Presets

| Module | Active Presets | Inactive Presets | Utilization Rate |
| :--- | :---: | :---: | :---: |
| Oscillator A (Osc 1) | 111 | 1 | 99.1% |
| Oscillator B (Osc 2) | 13 | 99 | 11.6% |
| Oscillator C (Osc 3) | 0 | 112 | 0.0% |
| Sub Oscillator | 0 | 112 | 0.0% |
| Noise Generator | 5 | 107 | 4.5% |
| Filter 1 | 112 | 0 | 100.0% |
| Filter 2 | 0 | 112 | 0.0% |
| Mixer Bus 1 | 0 | 112 | 0.0% |
| Mixer Bus 2 | 0 | 112 | 0.0% |

### Effect Usage Across 112 Presets

| Effect Module | Active Presets | Utilization Rate | Status in Sound Set |
| :--- | :---: | :---: | :--- |
| Reverb | 89 | 79.5% | Used |
| Chorus | 67 | 59.8% | Used |
| Delay | 51 | 45.5% | Used |
| Distortion | 40 | 35.7% | Used |
| Phaser | 17 | 15.2% | Used |
| Utility | 2 | 1.8% | Used |
| Hyperdim | 1 | 0.9% | Used |
| Bode | 1 | 0.9% | Used |
| Compressor | 0 | 0.0% | **COMPLETELY UNUSED (0 presets)** |
| Eq | 0 | 0.0% | **COMPLETELY UNUSED (0 presets)** |
| Flanger | 0 | 0.0% | **COMPLETELY UNUSED (0 presets)** |
| Convolve | 0 | 0.0% | **COMPLETELY UNUSED (0 presets)** |

### Kind of Thing Parameter Breakdown

#### Oscillator Types & Wavetables

| Parameter | Name | Active Values & Counts | Explicit Unused Values |
| :--- | :--- | :--- | :--- |
| `osc1.type` | Osc 1 Type | Wavetable: 112 | Sample (idx 1), Granular (idx 2), Spectral (idx 3), Multisample (idx 4) |
| `osc2.type` | Osc 2 Type | Wavetable: 112 | Sample (idx 1), Granular (idx 2), Spectral (idx 3), Multisample (idx 4) |
| `osc3.type` | Osc 3 Type | Wavetable: 112 | Sample (idx 1), Granular (idx 2), Spectral (idx 3), Multisample (idx 4) |
| `osc1.wavetable` | Osc 1 Wavetable | Sine: 9, Saw: 37, Square: 10, Triangle: 8, Pulse 25%: 6, Saw to Square: 6, Sine to Saw: 4, Harmonic Series: 7, Odd Harmonics: 3, Formant Sweep: 6, FM Sweep: 4, PWM Sweep: 4, Bell Partials: 8 | Preset table (idx 13), Saw Down (idx 14) |
| `osc2.wavetable` | Osc 2 Wavetable | Sine: 1, Saw: 102, Square: 2, Triangle: 1, Pulse 25%: 1, Saw to Square: 1, Harmonic Series: 2, Bell Partials: 2 | Sine to Saw (idx 6), Odd Harmonics (idx 8), Formant Sweep (idx 9), FM Sweep (idx 10), PWM Sweep (idx 11), Preset table (idx 13) (+1 more) |
| `osc3.wavetable` | Osc 3 Wavetable | Saw: 112 | Sine (idx 0), Square (idx 2), Triangle (idx 3), Pulse 25% (idx 4), Saw to Square (idx 5), Sine to Saw (idx 6) (+8 more) |
| `osc1.morph.type` | Osc 1 Morph Type | None: 111, Vocode: 1 | Formant Scale (idx 2), Harmonic Stretch (idx 3), Inharmonic Stretch (idx 4), Smear (idx 5), Random Amplitudes (idx 6), Low Pass (idx 7) (+4 more) |
| `osc2.morph.type` | Osc 2 Morph Type | None: 112 | Vocode (idx 1), Formant Scale (idx 2), Harmonic Stretch (idx 3), Inharmonic Stretch (idx 4), Smear (idx 5), Random Amplitudes (idx 6) (+5 more) |
| `osc3.morph.type` | Osc 3 Morph Type | None: 112 | Vocode (idx 1), Formant Scale (idx 2), Harmonic Stretch (idx 3), Inharmonic Stretch (idx 4), Smear (idx 5), Random Amplitudes (idx 6) (+5 more) |
| `osc1.gran.window_shape` | Osc 1 Grain Window Shape | Hann: 112 | Welch (idx 1), Gaussian (idx 2), Blackman-Harris (idx 3), Sinc (idx 4), Tukey (idx 5), Triangle (idx 6) (+3 more) |
| `osc2.gran.window_shape` | Osc 2 Grain Window Shape | Hann: 112 | Welch (idx 1), Gaussian (idx 2), Blackman-Harris (idx 3), Sinc (idx 4), Tukey (idx 5), Triangle (idx 6) (+3 more) |
| `osc3.gran.window_shape` | Osc 3 Grain Window Shape | Hann: 112 | Welch (idx 1), Gaussian (idx 2), Blackman-Harris (idx 3), Sinc (idx 4), Tukey (idx 5), Triangle (idx 6) (+3 more) |
| `osc1.sample.loop_mode` | Osc 1 Sample Loop Mode | One-shot: 112 | Forward loop (idx 1), Reverse loop (idx 2), Forward/reverse loop (idx 3), Tailed (idx 4) |
| `osc2.sample.loop_mode` | Osc 2 Sample Loop Mode | One-shot: 112 | Forward loop (idx 1), Reverse loop (idx 2), Forward/reverse loop (idx 3), Tailed (idx 4) |
| `osc3.sample.loop_mode` | Osc 3 Sample Loop Mode | One-shot: 112 | Forward loop (idx 1), Reverse loop (idx 2), Forward/reverse loop (idx 3), Tailed (idx 4) |

#### Oscillator Warp Types

| Parameter | Name | Active Values & Counts | Explicit Unused Values |
| :--- | :--- | :--- | :--- |
| `osc1.warp.type` | Osc 1 Warp Type | None: 105, Sync: 5, Formant: 2 | Quantize (idx 3), Bend (idx 4), Squeeze (idx 5), Pulse (idx 6), FM from Osc 2 (idx 7), FM from Osc 3 (idx 8) (+90 more) |
| `osc2.warp.type` | Osc 2 Warp Type | None: 112 | Sync (idx 1), Formant (idx 2), Quantize (idx 3), Bend (idx 4), Squeeze (idx 5), Pulse (idx 6) (+92 more) |
| `osc3.warp.type` | Osc 3 Warp Type | None: 112 | Sync (idx 1), Formant (idx 2), Quantize (idx 3), Bend (idx 4), Squeeze (idx 5), Pulse (idx 6) (+92 more) |
| `osc1.warp2.type` | Osc 1 Warp 2 Type | None: 111, Bend: 1 | Sync (idx 1), Formant (idx 2), Quantize (idx 3), Squeeze (idx 5), Pulse (idx 6), FM from Osc 2 (idx 7) (+91 more) |
| `osc2.warp2.type` | Osc 2 Warp 2 Type | None: 112 | Sync (idx 1), Formant (idx 2), Quantize (idx 3), Bend (idx 4), Squeeze (idx 5), Pulse (idx 6) (+92 more) |
| `osc3.warp2.type` | Osc 3 Warp 2 Type | None: 112 | Sync (idx 1), Formant (idx 2), Quantize (idx 3), Bend (idx 4), Squeeze (idx 5), Pulse (idx 6) (+92 more) |

#### Sub Oscillator & Noise Types

| Parameter | Name | Active Values & Counts | Explicit Unused Values |
| :--- | :--- | :--- | :--- |
| `sub.waveform` | Sub Waveform | Sine: 112 | Rounded Rect (idx 1), Triangle (idx 2), Saw (idx 3), Square (idx 4), Pulse (idx 5) |
| `noise.type` | Noise Type | Noise: 112 | Sample (idx 1) |
| `noise.sample` | Noise Sample | White Noise: 112 | Pink Noise (idx 1), Brown Noise (idx 2), Geiger (idx 3), Tape Hiss (idx 4), Vinyl Clicks (idx 5), Digital Hash (idx 6) (+2 more) |

#### Filter Models & Styles

| Parameter | Name | Active Values & Counts | Explicit Unused Values |
| :--- | :--- | :--- | :--- |
| `filter1.model` | Filter 1 Model | Analog: 2, Dirty: 2, Ladder: 3, Digital: 90, Diode: 2, Formant: 5, Comb: 4, Phaser: 1, Scream: 1, Diffusor: 1, EMS Ladder: 1 | Multi SVF (idx 8), Slope (idx 9), EQ 6 dB (idx 10), Ring Mod (idx 11), Sample Hold (idx 12), Smear (idx 13) (+10 more) |
| `filter2.model` | Filter 2 Model | Digital: 112 | Analog (idx 0), Dirty (idx 1), Ladder (idx 2), Diode (idx 4), Formant (idx 5), Comb (idx 6) (+20 more) |
| `fxfilter.model` | FX Filter Model | Digital: 112 | Analog (idx 0), Dirty (idx 1), Ladder (idx 2), Diode (idx 4), Formant (idx 5), Comb (idx 6) (+20 more) |
| `filter1.style` | Filter 1 Style | Style A (12 dB): 112 | Style B (24 dB) (idx 1), Style C (notch swap) (idx 2), Style D (dual notch) (idx 3), Style E (band peak notch) (idx 4), Style F (shelf) (idx 5), Style G (idx 6) (+5 more) |
| `filter2.style` | Filter 2 Style | Style A (12 dB): 112 | Style B (24 dB) (idx 1), Style C (notch swap) (idx 2), Style D (dual notch) (idx 3), Style E (band peak notch) (idx 4), Style F (shelf) (idx 5), Style G (idx 6) (+5 more) |
| `fxfilter.style` | FX Filter Style | Style A (12 dB): 112 | Style B (24 dB) (idx 1), Style C (notch swap) (idx 2), Style D (dual notch) (idx 3), Style E (band peak notch) (idx 4), Style F (shelf) (idx 5), Style G (idx 6) (+5 more) |

#### Filter Routing & Destinations

| Parameter | Name | Active Values & Counts | Explicit Unused Values |
| :--- | :--- | :--- | :--- |
| `filter.routing` | Filter Routing | Parallel: 112 | Series 1 to 2 (idx 1), Series 2 to 1 (idx 2) |
| `osc1.destination` | Osc 1 Destination | Filter 1: 112 | Filter 2 (idx 1), Filters 1+2 (idx 2), Effects only (idx 3), Direct out (idx 4), Filter blend (idx 5), None (idx 6) |
| `osc2.destination` | Osc 2 Destination | Filter 1: 112 | Filter 2 (idx 1), Filters 1+2 (idx 2), Effects only (idx 3), Direct out (idx 4), Filter blend (idx 5), None (idx 6) |
| `osc3.destination` | Osc 3 Destination | Filter 1: 112 | Filter 2 (idx 1), Filters 1+2 (idx 2), Effects only (idx 3), Direct out (idx 4), Filter blend (idx 5), None (idx 6) |
| `sub.destination` | Sub Destination | Effects only: 112 | Filter 1 (idx 0), Filter 2 (idx 1), Filters 1+2 (idx 2), Direct out (idx 4), Filter blend (idx 5), None (idx 6) |
| `noise.destination` | Noise Destination | Filter 1: 112 | Filter 2 (idx 1), Filters 1+2 (idx 2), Effects only (idx 3), Direct out (idx 4), Filter blend (idx 5), None (idx 6) |

#### FX Slot Effect Types

| Parameter | Name | Active Values & Counts | Explicit Unused Values |
| :--- | :--- | :--- | :--- |
| `fxi01.type` | FX Slot 01 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi02.type` | FX Slot 02 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi03.type` | FX Slot 03 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi04.type` | FX Slot 04 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi05.type` | FX Slot 05 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi06.type` | FX Slot 06 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi07.type` | FX Slot 07 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi08.type` | FX Slot 08 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi09.type` | FX Slot 09 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi10.type` | FX Slot 10 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi11.type` | FX Slot 11 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi12.type` | FX Slot 12 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi13.type` | FX Slot 13 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi14.type` | FX Slot 14 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi15.type` | FX Slot 15 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |
| `fxi16.type` | FX Slot 16 Type | None: 112 | Chorus (idx 1), Compressor (idx 2), Delay (idx 3), Distortion (idx 4), EQ (idx 5), FX Filter (idx 6) (+7 more) |

#### LFO Shapes & Sync Modes

| Parameter | Name | Active Values & Counts | Explicit Unused Values |
| :--- | :--- | :--- | :--- |
| `lfo1.shape` | LFO 1 Shape | Sine: 111, Custom: 1 | Triangle (idx 1), Saw Up (idx 2), Saw Down (idx 3), Square (idx 4), Pulse 25% (idx 5), Steps Up (idx 6) (+5 more) |
| `lfo2.shape` | LFO 2 Shape | Sine: 112 | Triangle (idx 1), Saw Up (idx 2), Saw Down (idx 3), Square (idx 4), Pulse 25% (idx 5), Steps Up (idx 6) (+6 more) |
| `lfo3.shape` | LFO 3 Shape | Sine: 112 | Triangle (idx 1), Saw Up (idx 2), Saw Down (idx 3), Square (idx 4), Pulse 25% (idx 5), Steps Up (idx 6) (+6 more) |
| `lfo4.shape` | LFO 4 Shape | Sine: 112 | Triangle (idx 1), Saw Up (idx 2), Saw Down (idx 3), Square (idx 4), Pulse 25% (idx 5), Steps Up (idx 6) (+6 more) |
| `lfo5.shape` | LFO 5 Shape | Sine: 112 | Triangle (idx 1), Saw Up (idx 2), Saw Down (idx 3), Square (idx 4), Pulse 25% (idx 5), Steps Up (idx 6) (+6 more) |
| `lfo6.shape` | LFO 6 Shape | Sine: 112 | Triangle (idx 1), Saw Up (idx 2), Saw Down (idx 3), Square (idx 4), Pulse 25% (idx 5), Steps Up (idx 6) (+6 more) |
| `lfo7.shape` | LFO 7 Shape | Sine: 112 | Triangle (idx 1), Saw Up (idx 2), Saw Down (idx 3), Square (idx 4), Pulse 25% (idx 5), Steps Up (idx 6) (+6 more) |
| `lfo8.shape` | LFO 8 Shape | Sine: 112 | Triangle (idx 1), Saw Up (idx 2), Saw Down (idx 3), Square (idx 4), Pulse 25% (idx 5), Steps Up (idx 6) (+6 more) |
| `lfo9.shape` | LFO 9 Shape | Sine: 112 | Triangle (idx 1), Saw Up (idx 2), Saw Down (idx 3), Square (idx 4), Pulse 25% (idx 5), Steps Up (idx 6) (+6 more) |
| `lfo10.shape` | LFO 10 Shape | Sine: 112 | Triangle (idx 1), Saw Up (idx 2), Saw Down (idx 3), Square (idx 4), Pulse 25% (idx 5), Steps Up (idx 6) (+6 more) |
| `lfo1.sync_type` | LFO 1 Trigger | Retrigger: 112 | Free-run (idx 1), Envelope (idx 2), Sustain envelope (idx 3), Loop at point (idx 4), Loop and hold (idx 5) |
| `lfo2.sync_type` | LFO 2 Trigger | Retrigger: 112 | Free-run (idx 1), Envelope (idx 2), Sustain envelope (idx 3), Loop at point (idx 4), Loop and hold (idx 5) |
| `lfo3.sync_type` | LFO 3 Trigger | Retrigger: 112 | Free-run (idx 1), Envelope (idx 2), Sustain envelope (idx 3), Loop at point (idx 4), Loop and hold (idx 5) |
| `lfo4.sync_type` | LFO 4 Trigger | Retrigger: 112 | Free-run (idx 1), Envelope (idx 2), Sustain envelope (idx 3), Loop at point (idx 4), Loop and hold (idx 5) |
| `lfo5.sync_type` | LFO 5 Trigger | Retrigger: 112 | Free-run (idx 1), Envelope (idx 2), Sustain envelope (idx 3), Loop at point (idx 4), Loop and hold (idx 5) |
| `lfo6.sync_type` | LFO 6 Trigger | Retrigger: 112 | Free-run (idx 1), Envelope (idx 2), Sustain envelope (idx 3), Loop at point (idx 4), Loop and hold (idx 5) |
| `lfo7.sync_type` | LFO 7 Trigger | Retrigger: 112 | Free-run (idx 1), Envelope (idx 2), Sustain envelope (idx 3), Loop at point (idx 4), Loop and hold (idx 5) |
| `lfo8.sync_type` | LFO 8 Trigger | Retrigger: 112 | Free-run (idx 1), Envelope (idx 2), Sustain envelope (idx 3), Loop at point (idx 4), Loop and hold (idx 5) |
| `lfo9.sync_type` | LFO 9 Trigger | Retrigger: 112 | Free-run (idx 1), Envelope (idx 2), Sustain envelope (idx 3), Loop at point (idx 4), Loop and hold (idx 5) |
| `lfo10.sync_type` | LFO 10 Trigger | Retrigger: 112 | Free-run (idx 1), Envelope (idx 2), Sustain envelope (idx 3), Loop at point (idx 4), Loop and hold (idx 5) |
| `rlfo1.sync_type` | Random LFO 1 Trigger | Retrigger: 112 | Free-run (idx 1) |
| `rlfo2.sync_type` | Random LFO 2 Trigger | Retrigger: 112 | Free-run (idx 1) |
| `rlfo3.sync_type` | Random LFO 3 Trigger | Retrigger: 112 | Free-run (idx 1) |
| `rlfo4.sync_type` | Random LFO 4 Trigger | Retrigger: 112 | Free-run (idx 1) |

#### Envelope Curves & Curve Settings

| Parameter | Name | Active Values & Counts | Explicit Unused Values |
| :--- | :--- | :--- | :--- |
| `env1.attack_curve` | Env 1 Attack Curve | 0: 112 | None |
| `env2.attack_curve` | Env 2 Attack Curve | 0: 112 | None |
| `env3.attack_curve` | Env 3 Attack Curve | 0: 112 | None |
| `env4.attack_curve` | Env 4 Attack Curve | 0: 112 | None |
| `env5.attack_curve` | Env 5 Attack Curve | 0: 112 | None |
| `env6.attack_curve` | Env 6 Attack Curve | 0: 112 | None |
| `env1.decay_curve` | Env 1 Decay Curve | -3: 112 | None |
| `env2.decay_curve` | Env 2 Decay Curve | -3: 112 | None |
| `env3.decay_curve` | Env 3 Decay Curve | -3: 112 | None |
| `env4.decay_curve` | Env 4 Decay Curve | -3: 112 | None |
| `env5.decay_curve` | Env 5 Decay Curve | -3: 112 | None |
| `env6.decay_curve` | Env 6 Decay Curve | -3: 112 | None |
| `env1.release_curve` | Env 1 Release Curve | -3: 112 | None |
| `env2.release_curve` | Env 2 Release Curve | -3: 112 | None |
| `env3.release_curve` | Env 3 Release Curve | -3: 112 | None |
| `env4.release_curve` | Env 4 Release Curve | -3: 112 | None |
| `env5.release_curve` | Env 5 Release Curve | -3: 112 | None |
| `env6.release_curve` | Env 6 Release Curve | -3: 112 | None |
| `curve.velocity.legato` | Velocity Curve Legato | Off: 112 | On (idx 1) |
| `curve.note.legato` | Note Curve Legato | Off: 112 | On (idx 1) |

#### Unison Voice Counts & Voicing Modes

| Parameter | Name | Active Values & Counts | Explicit Unused Values |
| :--- | :--- | :--- | :--- |
| `osc1.unison.voices` | Osc 1 Unison Voices | 1: 93, 2: 1, 3: 4, 4: 4, 5: 5, 6: 2, 7: 2, 8: 1 | 9, 10, 11, 12, 13, 14 (+2 more) |
| `osc2.unison.voices` | Osc 2 Unison Voices | 1: 110, 6: 1, 7: 1 | 2, 3, 4, 5, 8, 9 (+7 more) |
| `osc3.unison.voices` | Osc 3 Unison Voices | 1: 112 | 2, 3, 4, 5, 6, 7 (+9 more) |
| `osc1.unison.stack` | Osc 1 Unison Stack | Unison: 112 | Center Drop 12 (idx 1), Center Drop 24 (idx 2), Octaves (idx 3), Two Octaves (idx 4), Power Chord (idx 5), Two Power Chords (idx 6) (+4 more) |
| `osc2.unison.stack` | Osc 2 Unison Stack | Unison: 112 | Center Drop 12 (idx 1), Center Drop 24 (idx 2), Octaves (idx 3), Two Octaves (idx 4), Power Chord (idx 5), Two Power Chords (idx 6) (+4 more) |
| `osc3.unison.stack` | Osc 3 Unison Stack | Unison: 112 | Center Drop 12 (idx 1), Center Drop 24 (idx 2), Octaves (idx 3), Two Octaves (idx 4), Power Chord (idx 5), Two Power Chords (idx 6) (+4 more) |
| `osc1.unison.mode` | Osc 1 Unison Mode | Linear: 112 | Super (idx 1), Exp (idx 2), Inv (idx 3), Random (idx 4) |
| `osc2.unison.mode` | Osc 2 Unison Mode | Linear: 112 | Super (idx 1), Exp (idx 2), Inv (idx 3), Random (idx 4) |
| `osc3.unison.mode` | Osc 3 Unison Mode | Linear: 112 | Super (idx 1), Exp (idx 2), Inv (idx 3), Random (idx 4) |
| `osc1.unison.stack_repeat` | Osc 1 Unison Stack Repeat | 1x: 112 | 2x (idx 1), 3x (idx 2) |
| `osc2.unison.stack_repeat` | Osc 2 Unison Stack Repeat | 1x: 112 | 2x (idx 1), 3x (idx 2) |
| `osc3.unison.stack_repeat` | Osc 3 Unison Stack Repeat | 1x: 112 | 2x (idx 1), 3x (idx 2) |
| `osc1.unison.law` | Osc 1 Unison Law | Vital: 112 | Serum (idx 1) |
| `osc2.unison.law` | Osc 2 Unison Law | Vital: 112 | Serum (idx 1) |
| `osc3.unison.law` | Osc 3 Unison Law | Vital: 112 | Serum (idx 1) |
| `hyperdim.unison` | Hyper Unison | 4: 111, 5: 1 | 0, 1, 2, 3, 6, 7 |

#### Voice, Polyphony & Glide Modes

| Parameter | Name | Active Values & Counts | Explicit Unused Values |
| :--- | :--- | :--- | :--- |
| `master.voice_override` | Master Voice Override | Kill: 112 | Steal (idx 1) |
| `master.voice.steal_priority` | Master Voice Steal Priority | Newest: 112 | Oldest (idx 1), Highest (idx 2), Lowest (idx 3), Velocity (idx 4) |
| `master.voice.same_note` | Master Same Note Mode | Steal: 112 | Stack (idx 1), Ignore (idx 2) |
| `master.legato` | Master Legato | Off: 111, On: 1 | None |
| `master.portamento.always` | Portamento Always | Off: 112 | On (idx 1) |
| `master.portamento.scaled` | Portamento Scaled By Interval | Off: 112 | On (idx 1) |
| `sub.mono` | Sub Mono | Off: 112 | On (idx 1) |
| `voice.phase_legato` | Voice Phase Legato | Off: 112 | On (idx 1) |

#### MPE & Tuning Settings

| Parameter | Name | Active Values & Counts | Explicit Unused Values |
| :--- | :--- | :--- | :--- |
| `master.mpe.on` | MPE On | Off: 111, On: 1 | None |
| `master.mpe.xyz_macro` | MPE XYZ to Macros | Off: 112 | XYZ -> Macro 1, 2, 3 (idx 1), YZ -> Macro 1, 2 (idx 2) |
| `master.mpe.y_modwheel` | MPE Y to Mod Wheel | Off: 112 | On (idx 1) |
| `master.mpe.bend_to_x` | MPE Pitch Bend also to X | Off: 112 | On (idx 1) |
| `master.mpe.y_bipolar` | MPE Y Bi-directional | Off: 112 | On (idx 1) |
| `master.tuning.scale` | Tuning Scale | 12-TET: 111, Just intonation: 1 | Pythagorean (idx 1), User file (idx 3) |
| `master.tuning.mts_esp` | Tuning MTS-ESP | Off: 112 | On (idx 1) |
| `master.tuning.mts_note_on_only` | Tuning MTS-ESP Note-On Only | Off: 112 | On (idx 1) |
| `master.scale.key` | Scale Key | C: 112 | C# (idx 1), D (idx 2), D# (idx 3), E (idx 4), F (idx 5), F# (idx 6) (+5 more) |
| `master.scale.type` | Scale Type | Chromatic (off): 112 | Major (idx 1), Natural Minor (idx 2), Harmonic Minor (idx 3), Melodic Minor (idx 4), Dorian (idx 5), Phrygian (idx 6) (+13 more) |
| `master.scale.mode` | Scale Mode | Conform: 112 | Mark only (idx 1) |

### Modulation Matrix Utilization

- **Total Active Routes**: 1271
- **Sources**: 14 used / 43 unused (out of 57)
- **Destinations**: 33 used / 1215 unused (out of 1248)

#### Active Modulation Sources

| Source ID | Source Name | Key | Total Routes | Presets Using Source |
| :---: | :--- | :--- | :---: | :---: |
| 21 | Macro 3 | `macro3` | 279 | - |
| 20 | Macro 2 | `macro2` | 268 | - |
| 22 | Macro 4 | `macro4` | 255 | - |
| 19 | Macro 1 | `macro1` | 235 | - |
| 30 | Macro 6 | `macro6` | 89 | - |
| 7 | LFO 1 | `lfo1` | 76 | - |
| 31 | Macro 7 | `macro7` | 26 | - |
| 29 | Macro 5 | `macro5` | 21 | - |
| 15 | Random LFO 1 | `rlfo1` | 11 | - |
| 32 | Macro 8 | `macro8` | 5 | - |
| 2 | Env 2 | `env2` | 2 | - |
| 38 | Poly Aftertouch | `poly_aftertouch` | 2 | - |
| 1 | Env 1 | `env1` | 1 | - |
| 36 | Slide | `slide` | 1 | - |

#### Unused Modulation Sources (43 total)

Env 3 (3), Env 4 (4), Env 5 (5), Env 6 (6), LFO 2 (8), LFO 3 (9), LFO 4 (10), LFO 5 (11), LFO 6 (12), LFO 7 (13), LFO 8 (14), Random LFO 2 (16), Random LFO 3 (17), Random LFO 4 (18), Velocity (23), Note (24), Aftertouch (25), Pitch Wheel (26), Mod Wheel (27), Random per Note (28), LFO 9 (33), LFO 10 (34), Lift (35), Oct Note (37), Stereo (39), Voice Index (40), Voice Mod 1 (41), Voice Mod 2 (42), Oscillator A (43), Oscillator B (44), Oscillator C (45), Noise (46), Sub (47), Filter 1 (48), Filter 2 (49), Active Voices (50), Note-On Alt 1 (51), Note-On Alt 2 (52), Note-On Rand 2 (53), Note-On Rand (Discrete) (54), Release Velocity (55), Expression X (56), Fixed (57)

### Macro Target Routing Breakdown

| Macro | Presets Defining Targets | Total Routes | Favoured Destinations |
| :---: | :---: | :---: | :--- |
| Macro 1 | 112 | 235 | Filter 1 Cutoff (112), Filter 2 Cutoff (23), Filter 1 Resonance (23), Osc 1 Morph Amount (22) |
| Macro 2 | 112 | 268 | Osc 1 Warp Amount (45), Osc 1 Frame (45), LFO 1 Rate (28), Filter 1 Drive (23) |
| Macro 3 | 112 | 279 | LFO 1 Rate (46), Env 1 Release (40), Env 2 Decay (23), Env 1 Attack (23) |
| Macro 4 | 112 | 255 | Reverb Mix (89), Chorus Mix (63), Delay Mix (46), Osc 1 Unison Detune (23) |
| Macro 5 | 19 | 21 | Osc 1 Unison Stereo Spread (19), Osc 2 Unison Stereo Spread (2) |
| Macro 6 | 89 | 89 | Filter 1 Resonance (89) |
| Macro 7 | 13 | 26 | Osc 2 Unison Detune (13), Osc 2 Warp Amount (13) |
| Macro 8 | 5 | 5 | Noise Level (5) |

## 5. Performance Response

| Performance Source | Presets Routing Source | Percentage of Sound Set |
| :--- | :---: | :---: |
| Velocity | 0 | 0.0% |
| Release Velocity | 0 | 0.0% |
| Aftertouch (Channel) | 0 | 0.0% |
| Polyphonic Aftertouch | 1 | 0.9% |
| Mod Wheel | 0 | 0.0% |
| Pitch Wheel | 0 | 0.0% |
| Slide / Lift / Expression | 1 | 0.9% |
| Key Tracking | 0 | 0.0% |
| Random per Note | 0 | 0.0% |
| **ANY Performance Source** | **1** | **0.9%** |

- **Master Velocity Tracking (`master.velocity_track`)**: {100.0: 112}

## 6. Housekeeping & Matrix Integrity

- **Routes to Disabled Modules**: 79
- **Routes with Zero Amount**: 0
- **Macros with Targets but Zero Range**: 0
- **Identical Presets (Different Names)**: 0
- **Near-Identical Presets (<= 6 Differences)**: 50

### Disabled Module Route Breakdown (Sample)

| Category | Preset | Slot | Target Parameter | Target Module | Switch State |
| :--- | :--- | :---: | :--- | :--- | :---: |
| Bass | 808 Boom | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | Acid Saw Reso | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | Click Attack Bass | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | Comb Metal Bass | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | Custom Shape Wobble | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | Diode Saturated Bass | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | Dirty Distortion Bass | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | EMS Squelch Bass | 04 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | Fast Arp Bass | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | Fat Reese | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | Formant Vowel Bass | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | Gritty Square Bass | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | Harmonic Sine Bass | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | Moog Ladder Bass | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| Bass | Odd Harmonics Bass | 03 | `filter2.cutoff` | `filter2` | `filter2.on=0.0` |
| ... | *(+64 more routes)* | | | | |

## 7. Master Volume Level Distribution

| Scope | Count | Min (dB) | Max (dB) | Mean (dB) | Median (dB) | Std Dev |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Overall** | 112 | -27.8 | 2.9 | -12.0 | -13.2 | 6.7 |
| Bass | 23 | -23.7 | 2.9 | -13.7 | -14.9 | 6.4 |
| FX | 11 | -20.8 | -10.1 | -15.2 | -16.4 | 3.2 |
| Keys | 17 | -22.1 | -3.8 | -13.1 | -14.0 | 5.6 |
| Lead | 22 | -27.8 | -2.0 | -14.7 | -14.8 | 5.6 |
| Pad | 23 | -20.3 | 2.8 | -11.5 | -12.5 | 6.7 |
| Pluck | 16 | -7.4 | 1.2 | -3.1 | -3.5 | 2.2 |
