# Sound Quality, Device Profiles & Hardware Generations

SynthyBoy selects its profile from the model reported by the device. The Key 61 and XL have measured profiles. Other devices use the low profile, including models whose name cannot be read. See the [device table](../../shell-mpc/device_profile.h) and [installation guide](INSTALL.md).

---

## 1. Quick Summary: Gen 1 (MPC Key 61) vs. Gen 2 (MPC XL)

The hardware differences between generations reflect the available processing power of their CPUs:

| Feature / Setting | MPC XL (Gen 2) | MPC Key 61 | Practical Impact for Musicians |
|---|---|---|---|
| **CPU Architecture** | 64-bit AArch64 (8-core RK3588, audio runs on A76 cores @ 2.4 GHz) | 32-bit ARMv7 (Cortex-A17 @ 1.6 GHz) | The two devices have different voice and unison limits. |
| **Quality Profile** | `high,voices=16,control=economy` | `low,startbudget=900` | The XL runs the high engine tier; Key 61 runs the low engine tier. |
| **Polyphony Cap** | **16 voices** | **8 voices** | Two-handed chords with sustain pedal steal voices earlier on Key 61. |
| **Unison Voices** | Up to **16 unison voices** per oscillator | Capped at **3 unison voices** per oscillator | Supersaws and detuned pads are denser on XL, leaner on Key 61. |
| **Active Effects** | **All 16 effects run** | **5 heavy effects are skipped**: Reverb, Flanger, Hyper/Dimension, Bode, and Convolve | Presets using Reverb, Convolve, or Bode sound drier or quieter on Key 61. |
| **Warp Quality** | **4x oversampled** (128-tap Kaiser decimator) | **2x oversampled** (32-tap Kaiser decimator) | Extreme wavetable warps are cleaner and freer of aliasing on XL. |
| **Spectral Morph** | Full rate (updates every 256 samples) | Half rate (updates every 512 samples) | Rapidly modulated morphs lag by ~12 ms on Key 61 vs. ~6 ms on XL. |
| **Math Functions** | Exact mathematical calculation | Fast polynomial / rational approximations | The Key 61 uses approximations in selected DSP functions. |
| **Pad Expression** | Polyphonic pad pressure + Note Aftertouch | Key aftertouch only (no per-pad pressure) | XL pads allow per-finger filter and vibrato modulation while playing chords. |

---

## 2. Parameter & Project Compatibility

A core design rule of SynthyBoy is that **projects move between devices unchanged**:
- Presets (`.sbpreset`) and project save states (`.xpj`) store only your knob positions, matrix routings, and musical settings.
- No device-dependent profile settings or voice caps are ever written into project files.
- Each device applies its voice limits and effect rules to the stored preset when it plays. A chord may lose voices earlier on the Key 61, and a bypassed effect can change the sound.

---

## 3. Which Effects Are Skipped on Gen 1 (MPC Key 61)?

The low profile bypasses the following five effects by default.

1. **Reverb**: The algorithmic reverb processor is bypassed by default.
   - *Musician Override*: The Reverb screen includes a **FORCE ON** switch. If you require reverb on the Key 61, turn FORCE ON to On. This setting is saved with the preset. Note that enabling reverb consumes CPU, reducing the number of complex voices you can play without crackling.
2. **Convolve**: Zero-latency partition convolution (cabinet simulations, plate reverbs, and acoustic spaces) is bypassed.
3. **Bode**: The recursive Bode frequency shifter is bypassed.
4. **Hyper/Dimension**: Detuned multi-voice widening and dimension expansion are bypassed.
5. **Flanger**: Resonant comb-delay flanging is bypassed.

### Effects That Run on All Devices
The following effects are available on both devices. On the Key 61, ordinary effects are admitted in chain order up to an estimated 35% of an audio block. Later effects can be bypassed. Splitters and forced reverb remain active. [FX budget and device results](../dsp-knowledge/fx-budget.md)
- **Chorus** (stereo multi-voice chorus)
- **Delay** (stereo, ping-pong, and tempo-synced echoes)
- **Distortion** (14 distortion models, tube drive, clipping, and bit-crush)
- **Compressor** (single-band and multiband compression, peak limiter)
- **EQ** (4-band parametric equalizer)
- **FX Filter** (analog, ladder, diode, and formant filter models)
- **Phaser** (multi-stage sweeping phaser)
- **Utility** (stereo width, M/S balance, and mono bass filter below 150 Hz)
- **Splitters** (L/H, L/M/H, and M/S frequency splitters and band effect racks)

---

## 4. What This Means for Specific Factory Presets

The architectural differences between profiles produce distinct, audible differences on certain factory presets. Below is a breakdown of affected presets from Sound Set Batch 1, Sound Set Batch 2, and the initial library:

### 1. Presets Relying on Convolve (Quieter and Drier on Key 61)

In Sound Set Batch 2, several presets use the Convolve effect to add amplifier cabinet resonance or acoustic room reflections. On the MPC Key 61, where Convolve is bypassed, these presets play drier and at a lower level:

- **Reese Dual Phase Bass**: Uses guitar cabinet convolution (`cabinet_colour.wav`) at 55% mix. On the Key 61, Convolve is bypassed: the sound is **6.2 dB quieter** (-26.2 dBFS vs. -20.0 dBFS reference) and loses its 2.5 kHz speaker bite. Turn up track volume or Macro 3 (Growl) to compensate.
- **Dusty Rhodes Keys**: Uses the guitar cabinet IR at 85% mix to simulate an amplified Rhodes suitcase piano. On the Key 61, Convolve is bypassed: the preset is **5.3 dB quieter** (-25.3 dBFS vs. -20.0 dBFS) and sounds like a direct DI tine piano rather than an amplified cabinet.
- **Marimba Layer Keys**: Uses small room convolution (`small_room.wav`). On the Key 61, Convolve is bypassed: the preset is **3.1 dB quieter** (-23.1 dBFS vs. -20.0 dBFS) and lacks acoustic room reflection glue.
- **Frozen Pluck Drone**: Routes a sustained granular pluck through a wet-only plate convolution (`plate.wav`) on Bus 1. On the Key 61, the plate bus is absent, making the sound +2.9 dB louder in direct level while missing the reverberant halo.
- **Glass Ping Pluck**, **Glass Zone Bells**, **Breakbeat Slice Keys**, and **Reverse Gong Swell**: Convolve plate and room tails are bypassed on the Key 61. The instruments remain punchy and clear, but sound dry.

### 2. Presets Relying on the Bode Frequency Shifter

- **Bode Barber Riser** (Batch 1 FX): Uses recursive Bode frequency shifting to create an infinite Barber-pole upward pitch climb. On the Key 61, Bode is bypassed: the riser loses its frequency-shifting wash, and output level drops from -20.0 dBFS to -16.0 dBFS.
- **Bode Shifter Drone** (Initial 112 Texture): Relies on slow frequency shifting for metallic feedback motion; on the Key 61, the shifting effect is inactive.

### 3. Presets Relying on Reverb

- **Detonation Tail** (Batch 1 FX): The explosive initial sub hit sounds on both machines, but the cavernous 4-second reverb tail is silent on the Key 61 unless **FORCE ON** is enabled on the Reverb page.
- **Lorenz Garden** (Batch 1 Pad): The chaotic generative bell modulations sound identical, but the lush ambient reverb wash is bypassed on the Key 61.
- **Cathedral Choir**, **Ethereal Glass**, **Harmonic Cloud**, **Infinite Horizon**, and **Starlight Shimmer** (Initial 112 Pads): These presets rely on large hall reverb algorithms. On the Key 61, their choruses and delays remain active, but the reverberant tails are bypassed.

### 4. Presets Relying on Hyper/Dimension

- **Hyper Dimension Pad** (Initial 112 Pad): Built around the Hyper/Dimension effect. On the Key 61, the 5-voice Hyper detune and Dimension stereo width expansion are bypassed, resulting in a narrower stereo image.

### 5. Presets Affected by the Unison Cap (3 Voices on Key 61 vs. Up to 16 on XL)

Fourteen factory presets request between 4 and 7 unison voices per oscillator. On the Key 61, the engine caps unison at **3 voices per oscillator**:

- **Hyper Saw Lead**: Asks for 2 oscillators with 7 unison voices each (14 voices per note). On the Key 61, this is capped to 3 voices each (6 voices per note). The lead remains punchy and in tune, but the wall-of-sound stadium trance texture is noticeably leaner.
- **Lush Analog Pad**: Asks for 6 unison voices per oscillator (12 voices per chord note). On the Key 61, it plays with 3 voices per oscillator (6 voices per note).
- **Fat Reese**: Asks for 5 unison voices; capped to 3 on the Key 61.
- **Diffusor Cloud Pad**: Asks for 5 unison voices; capped to 3 on the Key 61.
- Other affected presets: `Cosmic Drift Lead`, `Ethereal Glass`, `Frozen Aurora`, `Harmonic Cloud`, `Infinite Horizon`, `Space Odyssey Pad`, `Tri-Band Split Texture`, `Velvet Strings`, `Vintage Brass Pad`, and `Pizzicato Strings`.

*Note on Unison Tuning*: Even with the 3-voice cap on the Key 61, the detune spread, stereo width, and overall level law remain balanced within 0.5 to 3 dB of the XL render.

### 6. Presets Affected by Voice Stealing (8 Voices on Key 61 vs. 16 on XL)

On the MPC Key 61, playing large two-handed chords (9 or more notes) or holding sustained chords with the damper pedal will steal the oldest sounding voices once 8 voices are active. This is particularly noticeable on slow pads with long release envelopes:
- `Lush Analog Pad`, `Cathedral Choir`, `Chord Cloud Pad`, and `Bowed MPE Strings`
- On the MPC XL, you can play up to 16 simultaneous voices before voice stealing occurs.

---

Sounds keep their loudness on the smaller device. When the MPC Key 61 switches off an effect that a preset uses, the engine makes up its level, so the preset is about as loud as on the MPC XL. The character of the missing effect, such as the room of a reverb, is still missing. The unison cap of the Key 61 (3 voices) gets no level makeup: a preset that asks for more voices can sound a little louder or quieter than on the XL (Lush Analog Pad and Infinite Horizon about 2 to 3 dB quieter, Hyper Saw Lead about half a dB louder), because how loud the beating of the detuned voices is differs from preset to preset and a gain would help some and harm others.

## Output Level and the Output Limiter (All Devices)

The shared output stage defaults to 0 dB gain and LIMIT off. Enabling LIMIT on the Master page adds 3 ms of look-ahead and applies a −0.5 dBFS peak ceiling. With LIMIT off, audio passes through without that delay. [Output stage](../../core/include/synthyboy/output_stage.h)

## 5. Practical Performance Tips for MPC Key 61 Users

1. **Missing Convolve on Key 61**: `Dusty Rhodes Keys` keeps its level on the Key 61 (within half a dB) and `Reese Dual Phase Bass` is within about 1.7 dB of the XL (its unison cap is not made up). The cabinet colour of the Convolve effect is still missing, so add an external MPC track insert effect (such as the stock MPC AIR Tube Drive) if you want it.
2. **Use FORCE ON for Essential Reverbs**: If a pad sounds too dry, navigate to the **FX** tab, select **Reverb**, and toggle **FORCE ON** to On. Keep an eye on voice count to ensure the audio block remains clean.
3. **Turn Off Q-Link Status Pop-Up**: On the MPC Key 61, the host's optional "Show Q-Link Status When Touched" overlay can cause the experimental touchscreen overlay to flicker when touching knobs. Turn it off in **Preferences > General > Show Q-Link Status When Touched** (or hold the hardware Q-Link button and tap "Q-Link Status").
4. **Use Key Aftertouch**: While the Key 61 lacks expressive pads with polyphonic pressure, its keyboard supports channel aftertouch. Assign Aftertouch to Macro 1 or Filter Cutoff in the modulation matrix for hands-on expressive control.
