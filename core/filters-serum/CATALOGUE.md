# Serum Filter Collection – Catalogue

This catalogue documents every filter type listed in the Serum 2 User Guide (Chapter "Using the Filter Modules", pages 133–143) implemented in `synthyboy::filters`.

Each entry details:
1. **Exact Manual Name**: The verbatim name in Serum.
2. **Family**: Grouping (Normal, Multi, Flanges, Misc, New).
3. **Topology**: The DSP structure implemented (e.g. SVF, Moog ladder, diode ladder, cascaded biquad/SVF, delay-line comb, all-pass diffuser, formant resonator bank, etc.).
4. **Parameter Meaning**:
   - `Cutoff`: Primary frequency / center parameter (Hz, log scale 20 Hz .. 20000 Hz).
   - `Resonance`: Q factor, feedback amount, or shape sharpening [0.0 .. 1.0].
   - `Drive`: Saturation / input gain boost [0.0 .. 1.0].
   - `Var`: Contextual 4th parameter as specified in the manual (FAT, FREQ, MORPH, LP FREQ, HP FREQ, HL WID, DB, SPREAD, DAMP, BOEUF, THRU, FORMNT, WIDTH, COMBFRQ, SCREAM, STAGES, SMOOTH, PAIN, FRQ2, MIX).
   - `Blend`: Sub-mode morph, dry/wet, or secondary blend [0.0 .. 1.0].

---

## 1. Normal Family (18 Types)

All Normal filters feature the **FAT** Var parameter, which injects tanh/polynomial saturation into the resonance feedback path, taming peak excursion and adding warm harmonics.

| # | Exact Manual Name | Family | Topology Implemented | Cutoff Meaning | Resonance Meaning | Drive Meaning | Var (FAT) Meaning | Blend Meaning |
|---|-------------------|--------|----------------------|----------------|-------------------|---------------|-------------------|---------------|
| 1 | `MG Low 6` | Normal | 1-pole sub-stage of Moog transistor ladder | 6 dB/oct cutoff frequency | Peak resonance at cutoff | Pre-ladder drive / saturation | Feedback path saturation | Pass-through blend |
| 2 | `MG Low 12` | Normal | 2-pole sub-stage of Moog transistor ladder | 12 dB/oct cutoff frequency | Resonance Q | Pre-ladder drive | Feedback path saturation | Pass-through blend |
| 3 | `MG Low 18` | Normal | 3-pole sub-stage of Moog transistor ladder | 18 dB/oct cutoff frequency | Resonance Q | Pre-ladder drive | Feedback path saturation | Pass-through blend |
| 4 | `MG Low 24` | Normal | 4-pole classic Moog transistor ladder (ZDF) | 24 dB/oct cutoff frequency | Resonance Q (self-oscillation) | Pre-ladder drive | Feedback path saturation | Pass-through blend |
| 5 | `Low 6` | Normal | 1-pole State-Variable Low-Pass (TPT) | 6 dB/oct corner frequency | High-frequency damping | Pre-filter saturation | Feedback saturation | Blend to flat |
| 6 | `Low 12` | Normal | 2-pole State-Variable Low-Pass (TPT SVF) | 12 dB/oct corner frequency | Q factor | Pre-filter saturation | Feedback saturation | Blend to flat |
| 7 | `Low 18` | Normal | 3-pole cascade: 1-pole LP + 2-pole SVF LP | 18 dB/oct corner frequency | Q factor | Pre-filter saturation | Feedback saturation | Blend to flat |
| 8 | `Low 24` | Normal | 4-pole cascade: dual 2-pole SVF LP | 24 dB/oct corner frequency | Q factor | Pre-filter saturation | Feedback saturation | Blend to flat |
| 9 | `High 6` | Normal | 1-pole State-Variable High-Pass (TPT) | 6 dB/oct corner frequency | High-frequency resonance | Pre-filter saturation | Feedback saturation | Blend to flat |
| 10 | `High 12` | Normal | 2-pole State-Variable High-Pass (TPT SVF) | 12 dB/oct corner frequency | Q factor | Pre-filter saturation | Feedback saturation | Blend to flat |
| 11 | `High 18` | Normal | 3-pole cascade: 1-pole HP + 2-pole SVF HP | 18 dB/oct corner frequency | Q factor | Pre-filter saturation | Feedback saturation | Blend to flat |
| 12 | `High 24` | Normal | 4-pole cascade: dual 2-pole SVF HP | 24 dB/oct corner frequency | Q factor | Pre-filter saturation | Feedback saturation | Blend to flat |
| 13 | `Band 12` | Normal | 2-pole State-Variable Band-Pass (6 dB skirts) | Center frequency | Q / Bandwidth sharpness | Pre-filter saturation | Feedback saturation | Blend to flat |
| 14 | `Band 24` | Normal | 4-pole cascade: dual 2-pole SVF Band-Pass | Center frequency | Q / Bandwidth sharpness | Pre-filter saturation | Feedback saturation | Blend to flat |
| 15 | `Peak 12` | Normal | 2-pole State-Variable Bell / Peak | Center frequency | Bell peak gain & Q | Pre-filter saturation | Feedback saturation | Blend to flat |
| 16 | `Peak 24` | Normal | 4-pole cascade: dual 2-pole SVF Peak | Center frequency | Bell peak gain & Q | Pre-filter saturation | Feedback saturation | Blend to flat |
| 17 | `Notch 12` | Normal | 2-pole State-Variable Notch (LP + HP) | Notch center frequency | Notch depth & sharpness | Pre-filter saturation | Feedback saturation | Blend to flat |
| 18 | `Notch 24` | Normal | 4-pole cascade: dual 2-pole SVF Notch | Notch center frequency | Notch depth & sharpness | Pre-filter saturation | Feedback saturation | Blend to flat |

---

## 2. Multi Family (16 Types)

### Dual SVF Cascades (12 Types)
Two 2-pole SVF filters in series. Resonance applies equally to both filters.
Var parameter = **FREQ**: sets the cutoff frequency of the second SVF filter relative to the primary cutoff (spanning from -4 octaves to +4 octaves around primary cutoff).

| # | Exact Manual Name | Family | Topology Implemented | Cutoff Meaning | Resonance Meaning | Drive Meaning | Var (FREQ) Meaning | Blend Meaning |
|---|-------------------|--------|----------------------|----------------|-------------------|---------------|--------------------|---------------|
| 19 | `LH` | Multi | SVF1 Low-Pass + SVF2 High-Pass in series | Primary LP cutoff | Dual resonance Q | Pre-filter saturation | SVF2 HP cutoff frequency | Balance between stages |
| 20 | `LB` | Multi | SVF1 Low-Pass + SVF2 Band-Pass in series | Primary LP cutoff | Dual resonance Q | Pre-filter saturation | SVF2 BP cutoff frequency | Balance between stages |
| 21 | `LP` | Multi | SVF1 Low-Pass + SVF2 Peak in series | Primary LP cutoff | Dual resonance Q | Pre-filter saturation | SVF2 Peak frequency | Balance between stages |
| 22 | `LN` | Multi | SVF1 Low-Pass + SVF2 Notch in series | Primary LP cutoff | Dual resonance Q | Pre-filter saturation | SVF2 Notch frequency | Balance between stages |
| 23 | `HB` | Multi | SVF1 High-Pass + SVF2 Band-Pass in series | Primary HP cutoff | Dual resonance Q | Pre-filter saturation | SVF2 BP cutoff frequency | Balance between stages |
| 24 | `HP` | Multi | SVF1 High-Pass + SVF2 Peak in series | Primary HP cutoff | Dual resonance Q | Pre-filter saturation | SVF2 Peak frequency | Balance between stages |
| 25 | `HN` | Multi | SVF1 High-Pass + SVF2 Notch in series | Primary HP cutoff | Dual resonance Q | Pre-filter saturation | SVF2 Notch frequency | Balance between stages |
| 26 | `BP` | Multi | SVF1 Band-Pass + SVF2 Peak in series | Primary BP cutoff | Dual resonance Q | Pre-filter saturation | SVF2 Peak frequency | Balance between stages |
| 27 | `BN` | Multi | SVF1 Band-Pass + SVF2 Notch in series | Primary BP cutoff | Dual resonance Q | Pre-filter saturation | SVF2 Notch frequency | Balance between stages |
| 28 | `PP` | Multi | SVF1 Peak + SVF2 Peak in series | Primary Peak 1 frequency | Dual resonance Q | Pre-filter saturation | Peak 2 frequency | Balance between stages |
| 29 | `PN` | Multi | SVF1 Peak + SVF2 Notch in series | Primary Peak frequency | Dual resonance Q | Pre-filter saturation | Notch frequency | Balance between stages |
| 30 | `NN` | Multi | SVF1 Notch + SVF2 Notch in series | Primary Notch 1 frequency | Dual resonance Q | Pre-filter saturation | Notch 2 frequency | Balance between stages |

### Morphing SVF Filters (4 Types)
A single SVF engine where the Var parameter (**MORPH**) transitions smoothly between filter responses.

| # | Exact Manual Name | Family | Topology Implemented | Cutoff Meaning | Resonance Meaning | Drive Meaning | Var (MORPH) Meaning | Blend Meaning |
|---|-------------------|--------|----------------------|----------------|-------------------|---------------|---------------------|---------------|
| 31 | `LBH` | Multi | Continuous SVF response morph | Center/Corner frequency | Resonance Q | Saturation | LP (0.0) -> BP (0.5) -> HP (1.0) | Secondary mix |
| 32 | `LPH` | Multi | Continuous SVF response morph | Center/Corner frequency | Resonance Q | Saturation | LP (0.0) -> Peak (0.5) -> HP (1.0) | Secondary mix |
| 33 | `LNH` | Multi | Continuous SVF response morph | Center/Corner frequency | Resonance Q | Saturation | LP (0.0) -> Notch (0.5) -> HP (1.0) | Secondary mix |
| 34 | `BPN` | Multi | Continuous SVF response morph | Center/Corner frequency | Resonance Q | Saturation | BP (0.0) -> Peak (0.5) -> Notch (1.0) | Secondary mix |

---

## 3. Flanges Family (9 Types)

Comb, Flanger, and Phaser topologies containing an integrated filter inside the feedback loop.
- **Comb**: Delay line tuned to period $T = 1 / f_{\text{cutoff}}$.
- **Flanger**: Short tuned delay (0.2 ms to 10 ms) with high resonance.
- **Phaser**: 6-pole allpass cascade with feedback.
Var controls feedback filter frequency:
- `LP FREQ`: low-pass corner in feedback path.
- `HP FREQ`: high-pass corner in feedback path.
- `HL WID`: passband width around cutoff (HP + LP in feedback).

| # | Exact Manual Name | Family | Topology Implemented | Cutoff Meaning | Resonance Meaning | Drive Meaning | Var Meaning | Blend Meaning |
|---|-------------------|--------|----------------------|----------------|-------------------|---------------|-------------|---------------|
| 35 | `Cmb L` | Flanges | Comb delay line with 1-pole LP in feedback | Delay tuning (pitch) | Feedback gain / decay | Saturation | `LP FREQ`: Feedback LP cutoff | Delay polarity / fine blend |
| 36 | `Flg L` | Flanges | Short delay flanger with 1-pole LP in feedback | Flange center pitch | Flanger feedback depth | Saturation | `LP FREQ`: Feedback LP cutoff | Flange depth blend |
| 37 | `Phs L` | Flanges | 6-stage APF phaser with 1-pole LP in feedback | Phaser notch frequencies | Phaser feedback depth | Saturation | `LP FREQ`: Feedback LP cutoff | Dry/wet balance |
| 38 | `Cmb H` | Flanges | Comb delay line with 1-pole HP in feedback | Delay tuning (pitch) | Feedback gain / decay | Saturation | `HP FREQ`: Feedback HP cutoff | Delay polarity / fine blend |
| 39 | `Flg H` | Flanges | Short delay flanger with 1-pole HP in feedback | Flange center pitch | Flanger feedback depth | Saturation | `HP FREQ`: Feedback HP cutoff | Flange depth blend |
| 40 | `Phs H` | Flanges | 6-stage APF phaser with 1-pole HP in feedback | Phaser notch frequencies | Phaser feedback depth | Saturation | `HP FREQ`: Feedback HP cutoff | Dry/wet balance |
| 41 | `Cmb HL` | Flanges | Comb delay line with bandpass (HP+LP) in feedback | Delay tuning (pitch) | Feedback gain / decay | Saturation | `HL WID`: Feedback passband width | Delay polarity / fine blend |
| 42 | `Flg HL` | Flanges | Short delay flanger with bandpass in feedback | Flange center pitch | Flanger feedback depth | Saturation | `HL WID`: Feedback passband width | Flange depth blend |
| 43 | `Phs HL` | Flanges | 6-stage APF phaser with bandpass in feedback | Phaser notch frequencies | Phaser feedback depth | Saturation | `HL WID`: Feedback passband width | Dry/wet balance |

---

## 4. Misc Family (26 Types)

Specialized filters, distortions, reverbs, and vocal/formant processors.

| # | Exact Manual Name | Family | Topology Implemented | Cutoff Meaning | Resonance Meaning | Drive Meaning | Var Meaning | Blend Meaning |
|---|-------------------|--------|----------------------|----------------|-------------------|---------------|-------------|---------------|
| 44 | `Low EQ 6` | Misc | 1-pole low-shelving EQ | Shelf transition freq | Unused (6 dB/oct fixed) | Saturation | `DB +/-`: Passband boost/cut | Curve morphing |
| 45 | `Low EQ 12` | Misc | 2-pole low-shelving biquad | Shelf transition freq | Shelf resonance / overshoot | Saturation | `DB +/-`: Passband boost/cut | Curve morphing |
| 46 | `Band EQ 6` | Misc | 1-pole band EQ / tilt-peak | Center frequency | Unused (fixed bandwidth) | Saturation | `DB +/-`: Peak boost/cut | Symmetry |
| 47 | `Band EQ 12` | Misc | 2-pole parametric bell EQ | Center frequency | Bandwidth Q | Saturation | `DB +/-`: Peak boost/cut | Symmetry |
| 48 | `High EQ 6` | Misc | 1-pole high-shelving EQ | Shelf transition freq | Unused (6 dB/oct fixed) | Saturation | `DB +/-`: Passband boost/cut | Curve morphing |
| 49 | `High EQ 12` | Misc | 2-pole high-shelving biquad | Shelf transition freq | Shelf resonance / overshoot | Saturation | `DB +/-`: Passband boost/cut | Curve morphing |
| 50 | `Ring Mod` | Misc | Single carrier ring modulator | Carrier frequency | Carrier waveshape (sine to tri) | Input gain | N/A | Carrier mix |
| 51 | `Ring Mod x2` | Misc | Dual carrier ring modulator | Carrier 1 frequency | Carrier waveshape | Input gain | `SPREAD`: Carrier 2 freq offset | Balance carrier 1/2 |
| 52 | `SampHold` | Misc | Sample-and-hold rate reducer | Sampling clock frequency | Jitter / smoothing | Drive distortion | N/A | Bit depth reduction |
| 53 | `SampHold-` | Misc | S&H difference distortion ($\text{in} - \text{held}$) | Sampling clock frequency | Jitter / smoothing | Drive distortion | N/A | Blend with dry |
| 54 | `Combs` | Misc | Parallel comb filter phase smearer | Base delay frequency | Feedback resonance | Input drive | `DAMP`: HF feedback damping | Comb spacing spread |
| 55 | `Allpasses` | Misc | 8-stage nested all-pass phase smearer | APF center frequency | APF feedback gain | Input drive | `DAMP`: HF feedback damping | Diffusion decay rate |
| 56 | `Reverb` | Misc | Schroeder-Moorer mini reverb tank | Reverb pre-filter cutoff | Room size / decay time | Input drive | `DAMP`: HF damping in tank | Early/late reflections balance |
| 57 | `French LP` | Misc | Diode-clamped non-linear LP (Steiner-Parker) | Low-pass cutoff | Primary resonance | Non-linear drive | `BOEUF`: Secondary resonance | Saturation character |
| 58 | `German LP` | Misc | Clean ZDF Low-Pass filter | Low-pass cutoff | Resonance Q | Input saturation | N/A | Sub-bass compensation |
| 59 | `Add Bass` | Misc | Driven LP with phase-rotated dry signal | Low-pass cutoff | Low-frequency resonance | Bass overdrive | `THRU`: Phase-rotated dry level | Dry/wet balance |
| 60 | `Formant-I` | Misc | Triple parallel vowel resonator (Set 1: A-E-I-O-U) | Vowel sweep morph | Formant peak Q | Resonator drive | `FORMNT`: Formant shift/transpose | Vowel formant spread |
| 61 | `Formant-II` | Misc | Triple parallel vowel resonator (Set 2: A-I-U-O) | Vowel sweep morph | Formant peak Q | Resonator drive | `FORMNT`: Formant shift/transpose | Vowel formant spread |
| 62 | `Formant-III` | Misc | Vocal tract model formant filter | Vocal tract aperture / vowel | Formant peak Q | Resonator drive | `FORMNT`: Formant shift/transpose | Nasal tract resonance |
| 63 | `Bandreject` | Misc | Variable-bandwidth band-reject filter | Center frequency | Notch depth | Drive saturation | `WIDTH`: Notch attenuation width | Skirt steepness |
| 64 | `Dist.Comb 1 LP` | Misc | Comb filter + LP with positive feedback | Comb frequency | Feedback depth | Saturation drive | `COMBFRQ`: Comb delay frequency | LP corner frequency |
| 65 | `Dist.Comb 1 BP` | Misc | Comb filter + BP with positive feedback | Comb frequency | Feedback depth | Saturation drive | `COMBFRQ`: Comb delay frequency | BP bandwidth |
| 66 | `Dist.Comb 2 LP` | Misc | Comb filter + LP with negative feedback | Comb frequency | Feedback depth | Saturation drive | `COMBFRQ`: Comb delay frequency | LP corner frequency |
| 67 | `Dist.Comb 2 BP` | Misc | Comb filter + BP with negative feedback | Comb frequency | Feedback depth | Saturation drive | `COMBFRQ`: Comb delay frequency | BP bandwidth |
| 68 | `Scream LP` | Misc | Low-pass with screamed non-linear feedback | LP cutoff frequency | Resonance feedback | Drive (screams when > 50%) | `SCREAM`: Feedback loop cutoff | Feedback asymmetry |
| 69 | `Scream BP` | Misc | Band-pass with screamed non-linear feedback | BP center frequency | Resonance feedback | Drive (screams when > 50%) | `SCREAM`: Feedback loop cutoff | Feedback asymmetry |

---

## 5. New Family (11 Types)

Modern and vintage circuit-modelled filters.

| # | Exact Manual Name | Family | Topology Implemented | Cutoff Meaning | Resonance Meaning | Drive Meaning | Var Meaning | Blend Meaning |
|---|-------------------|--------|----------------------|----------------|-------------------|---------------|-------------|---------------|
| 70 | `New Wsp` | New | EDP Wasp CMOS inverting OTA filter | Cutoff frequency | Wasp buzzy resonance | CMOS overdrive | `MORPH`: LP -> Notch -> HP | Sub-oscillator buzz |
| 71 | `DJ Mixer Xfer` | New | DJM-style bipolar lowpass/highpass filter | Neutral at center, LP left, HP right | Filter resonance peak | Output saturation | N/A | Slope steepness |
| 72 | `Diffusor` | New | Multi-stage all-pass diffusion cascade | APF center frequency | Diffusion feedback | Saturation drive | `STAGES`: Number of APF stages (1..8) | Dry/wet diffuse balance |
| 73 | `MG Ladder` | New | Precision transistor ladder model | Ladder cutoff frequency | Ladder resonance | Symmetrical overdrive | `SMOOTH`: Cutoff slew rate | Thermal drift amount |
| 74 | `Acid Ladder` | New | TB-303 diode ladder model with HP feedback | 303 cutoff frequency | 303 squealy resonance | Diode distortion | `SMOOTH`: Cutoff slew rate | Bass roll-off compensation |
| 75 | `EMS Ladder` | New | EMS VCS3 diode matrix ladder filter | EMS cutoff frequency | Saturated resonance | Matrix overdrive | `SMOOTH`: Cutoff slew rate | Resonator asymmetry |
| 76 | `MG Dirty` | New | Overdriven Moog ladder with thermal clipping | Overdriven cutoff | Screaming resonance | Core saturation | `PAIN`: Circuit temperature / overdrive | Asymmetry bias |
| 77 | `PZ SVF` | New | Pole-Zero state-variable filter | Pole frequency | Pole resonance Q | Input saturation | `SMOOTH`: Cutoff slew rate | Zero frequency / placement |
| 78 | `Comb 2` | New | Dual-tap resonant comb filter | Primary comb tuning | Feedback gain | Non-linear drive | `FRQ2`: Secondary comb frequency | Tap 1 / Tap 2 balance |
| 79 | `Exp MM` | New | Oberheim SEM multimode filter | SEM cutoff frequency | SEM resonance | OTA input drive | `MIX`: LP -> Notch -> HP | Blend curve |
| 80 | `Exp BPF` | New | Oberheim SEM bandpass filter | SEM center frequency | SEM bandpass Q | OTA input drive | N/A | Skirt symmetry |

---

## 6. Implementation Summary

- **Total Types**: 80
- **Namespace**: `synthyboy::filters`
- **Classes**: `FilterParams`, `Filter`
- **Safety guarantees**:
  - Zero dynamic heap allocation in `process()`.
  - Denormal-safe arithmetic (`denormal-safe`).
  - Strict bounding against `NaN` and `Inf`.
  - Stable across full sample rate range (22,050 Hz to 192,000 Hz) and block sizes (1 to 4096 samples).
  - Clean internal one-pole parameter smoothing on cutoff and resonance to prevent modulation zippering and clicks.
