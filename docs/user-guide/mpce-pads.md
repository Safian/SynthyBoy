# MPCe Expressive 3D Pads (Gen 2) & SynthyBoy

This chapter describes how to use the expressive pads of second-generation Akai MPC hardware (MPC XL and MPC Live III) with SynthyBoy. It explains what physical gestures the hardware pads sense, how the MPC Standalone OS handles them, what MIDI messages actually reach a SynthyBoy plugin track, and how to configure SynthyBoy’s modulation matrix and global pad pressure layer for deeply expressive playing.

---

## 1. Hardware Generations: Gen 2 (MPCe) vs Gen 1

### 1.1 Second-Generation Hardware (MPC XL, MPC Live III)

Second-generation Akai standalone instruments feature redesigned, multi-dimensional sensor pads officially referred to by Akai as **MPCe pads** or **expressive pads** ([MPC 3.6 Firmware Update Release Notes, p. 1]; [MPC Live III, MPC XL User Guide v3.10.1, p. 4, p. 11]).

According to the official Akai manuals, MPCe hardware sensors capture three distinct physical dimensions simultaneously:
1. **Strike Velocity:** The initial impact force of the finger [User Guide v3.10.1, p. 63].
2. **Continuous Pad Pressure (Polyphonic Aftertouch):** Continuous downward pressure applied to each pad independently throughout the duration of a held note [User Guide v3.10.1, p. 64, 350; MPC 3.6, p. 1].
3. **Surface Finger Position (X and Y Axes):** The horizontal (X) and vertical (Y) coordinates of the finger across the pad surface [User Guide v3.10.1, p. 64, 350; MPC 3.6, p. 2].

From these dimensions, the MPC OS derives secondary performance metrics on internal Drum Tracks:
- **Pad XY Center Dist:** The radial distance of the touch from the center of the pad [MPC 3.6 Firmware Update Release Notes, p. 2].
- **Pad XY Press Dist:** The distance traveled across the pad from the initial strike coordinate [MPC 3.6, p. 2].
- **Pad dX / Pad dY:** The directional horizontal and vertical displacement from the initial touch point [MPC 3.6, p. 2].
- **Pad Quadrants:** The division of each pad into four discrete corner zones (lower-left, lower-right, upper-left, upper-right), used to trigger separate drum articulations (such as flams, buzzes, rolls, and paradiddles) or sample layers ([MPC Live III, MPC XL User Guide v3.10.1, p. 13, 22, 225, 234]; [MPC 3.6, p. 5]; [MPC 3.7 Update Release Notes, p. 2-3]).
- **8×8 (64 Pads) Performance Mode:** The ability to split the 4×4 physical grid into four simultaneous pad banks (Banks A–D or E–H), allowing 64 distinct notes or clip triggers at once ([MPC 3.10 Release Notes, p. 4-5]; [User Guide v3.10.1, p. 72, 380]).
- **MPCe Q-Link Mode:** Using the MPCe pads as touch-sensitive macro surfaces to modulate automation parameters ([MPC 3.7 Update Release Notes, p. 1-2]; [User Guide v3.10.1, p. 337, 350]).

### 1.2 First-Generation Hardware (MPC Key 61, MPC One, Live I/II, MPC X)

First-generation MPC standalone devices (e.g., MPC Key 61, MPC Key 37, MPC One, MPC One+, MPC Live I & II, MPC X, MPC X Special Edition) do **not** have MPCe pads [MPC Standalone OS User Guide v3.10.1, p. 7]:
- Their pads sense **strike velocity** and **pressure** only [Standalone OS User Guide v3.10.1, p. 62, 333].
- They have **no X/Y surface position tracking**, **no quadrant detection**, **no 8×8 pad splitting**, and **no MPCe Q-Link mode**.
- Keyboard models (such as the MPC Key 61) feature a semi-weighted keybed that transmits standard **Channel Aftertouch** (`0xD0`) [Standalone OS User Guide v3.10.1, p. 369, 479], which affects all sounding voices globally rather than individual notes.
- The 16 physical pads on Gen 1 devices can be configured in controller mode to send either Channel Pressure or Poly Aftertouch [Standalone OS User Guide v3.10.1, p. 333], but lack the continuous spatial sensitivity of Gen 2 MPCe pads.

---

## 2. Host Reality: What Reaches a Plugin Track

While the Akai manuals detail extensive modulation possibilities for MPCe pads within internal **Drum Tracks**, external VST2 plugin tracks are subject to the host's MIDI transmission pipeline.

The behavior below was measured directly on an MPC XL running MPC OS 3.10.1.12 with diagnostic MIDI logging in the `Sbtest` project ([docs/MPC.md, "MPC XL pads"](../MPC.md#mpc-xl-pads)):

| MIDI Event / Parameter | Full Level ON | Full Level OFF | Behavior on Plugin Tracks |
|---|---|---|---|
| **Note On / Note Off** | Yes | Yes | Notes arrive on MIDI Channel 1. |
| **Strike Velocity** | Always 127 | Dynamic (e.g. 96, 127) | Transmitted to SynthyBoy's **Velocity** source. |
| **Polyphonic Aftertouch (`0xA0`)** | **None (0 messages)** | **Continuous stream** | Stream of 1..127 values per held pad, approx. every 6 ms (~170 Hz). Transmitted to SynthyBoy's **Poly Aftertouch** source. |
| **Channel Pressure (`0xD0`)** | None (0) | None (0) | Not sent by the MPC pads to the plugin track. |
| **Pitch Bend** | None (0) | None (0) | Not sent by pad movements. |
| **CC Messages (including CC 74)** | None (0) | None (0) | Not sent by pad movements. |
| **MPE Channel Rotation** | None | None | All notes remain strictly on MIDI Channel 1. No MPE voice allocation. |

### 2.1 Key Takeaways for SynthyBoy Players

1. **Velocity and Per-Pad Pressure work natively:**
   - Hitting a pad sends **Velocity** (scaled by `master.velocity_track` on the Master VOICES block).
   - Pressing into a held pad sends per-voice **Poly Aftertouch** (`0xA0`), allowing each finger in a chord to modulate sound independently.
2. **Pad X/Y position does NOT reach the plugin:**
   - The MPC OS does not translate horizontal or vertical finger movement on the pads into standard MIDI CCs (such as CC 74 Brightness or Pitch Bend) for external plugins.
   - Modulation sources such as **Slide** (MPE Y / CC 74, choice 36), **Expression X** (choice 56), **Pitch Wheel** (choice 26), and **Mod Wheel** (choice 27) **do not move from pad gestures**.
3. **Full Level disables all pad pressure:**
   - When the hardware button **Full Level / Set Level** is enabled [User Guide v3.10.1, p. 389], or if **Preferences > General > Enable Pads User Level By Default** is checked [User Guide v3.10.1, p. 66], velocity is locked at 127 and the host transmits **zero aftertouch messages**.
   - > [!IMPORTANT]
     > To use expressive pad pressure with SynthyBoy, **Full Level must be switched OFF**.

### 2.2 Host Configuration Checklist

To ensure your MPC hardware transmits expressive data properly:
- **Preferences > Hardware:** Check **Pad Threshold**, **Pad Sensitivity**, and **Pad Curve** (Curve A is linear; B, C, and D are exponential) [User Guide v3.10.1, p. 63]. Use the on-screen **Velocity & Aftertouch** meters to calibrate your playing touch.
- **Preferences > Sequencer:** Ensure **Record Pad Aftertouch Events** is enabled (`On`) so that pressure modulations are captured during recording [User Guide v3.10.1, p. 64]. (Note: **Record Pad XY Events** only affects internal drum track automation).
- **Pad Perform:** When using Pad Perform, ensure aftertouch is enabled in the Pad Perform setup window (`Notes / Pad CTRL` or the **Use Aftertouch** checkbox) [User Guide v3.10.1, p. 83].

---

## 3. SynthyBoy Modulation Sources: What Moves and What Does Not

SynthyBoy provides a 64-slot modulation matrix with 57 modulation sources ([core/src/modulation.cpp](../../core/src/modulation.cpp); [docs/params.json](../params.json)):

| Source ID | Modulation Source Name | Type | Receives Data from MPC Pads? | Notes / Destination Behavior |
|---|---|---|---|---|
| **23** | `velocity` (Velocity) | Unipolar (`0..1`) | **YES** | Initial strike velocity per voice. Scaled by `master.velocity_track` (Master VOICES block: `VEL TRACK`). |
| **38** | `poly_aftertouch` (Poly Aftertouch) | Unipolar (`0..1`) | **YES** | Per-pad, per-voice continuous aftertouch stream (`0xA0`, values 0.0 to 1.0). Evaluated per voice in real time (`core/engine-own/voice/voice.cpp:645`). |
| **25** | `aftertouch` (Aftertouch) | Unipolar (`0..1`) | **NO (Pads)** | Global Channel Pressure (`0xD0`). Received from keyboard keybeds (such as MPC Key 61), but not from MPC pads. |
| **36** | `slide` (Slide / MPE Y) | Unipolar (`0..1`) | **NO** | Driven by MIDI CC 74 or MPE Y. Does not receive pad Y-axis position. |
| **56** | `expression_x` (Expression X) | Bipolar (`-1..+1`) | **NO** | Driven by MPE note expression (Pan/X). Does not receive pad X-axis position. |
| **35** | `lift` (Lift / Release Velocity) | Unipolar (`0..1`) | **NO / Unverified** | Note-off release velocity. MPC pads typically transmit fixed note-off velocity. |
| **26** | `pitch_wheel` (Pitch Wheel) | Bipolar (`-1..+1`) | **NO** | Physical pitch wheel or touch strip only. |
| **27** | `mod_wheel` (Mod Wheel) | Unipolar (`0..1`) | **NO** | Physical mod wheel or touch strip only. |

---

## 4. Master Page Settings: Tuning & MPE

SynthyBoy’s Master page contains a dedicated **TUNING & MPE** side ([docs/MPC.md, line 1142](../MPC.md); [docs/params.json](../params.json)):

```
+-----------------------------------------------------------------------+
|  MASTER: TUNING & MPE                                                 |
|  [ SCALE & KEYBOARD ]        [ TUNING ]            [ MPE ]            |
|  Scale: 12-TET               Tune: 440 Hz          MPE ON: Off        |
|  Root: C                     MTS-ESP: Off          BEND RANGE: 48 st  |
|                                                    XYZ TO MACROS: Off |
|                                                    Y TO WHEEL: Off    |
|                                                    BEND TO X: Off     |
|                                                    Y BIPOLAR: Off     |
+-----------------------------------------------------------------------+
```

### The MPE Parameter Set
- **MPE On** (`master.mpe.on`): Enables multi-channel MPE voice allocation.
- **MPE Pitch Bend Range** (`master.mpe.bend_range`): Member channel bend range (1 to 96 semitones, default 48).
- **MPE XYZ to Macros** (`master.mpe.xyz_macro`): Maps MPE dimensions to Macros: `Off`, `XYZ -> Macro 1, 2, 3`, or `YZ -> Macro 1, 2`.
- **MPE Y to Mod Wheel** (`master.mpe.y_modwheel`): Routes MPE Y to the global Mod Wheel bus.
- **MPE Pitch Bend also to X** (`master.mpe.bend_to_x`): Routes member pitch bend to the Expression X modulation source.
- **MPE Y Bi-directional** (`master.mpe.y_bipolar`): Treats Y as bipolar centered around zero.

> [!WARNING]
> **Leave MPE OFF when performing with the MPC hardware pads.**
> The MPC hardware pads do **not** transmit multi-channel MPE data to external plugins; all notes arrive on MIDI Channel 1. Enabling `MPE On` or `MPE XYZ to Macros` will not make the pads control Macros or Expression axes, and may lead to unexpected voice allocation behavior. Reserve MPE mode for external MPE controllers (such as a Roli Seaboard, LinnStrument, or Expressive E Osmose) connected to the MPC's USB host port.

---

## 5. Two Ways to Use Pad Pressure in SynthyBoy

SynthyBoy provides two distinct mechanisms for pad pressure:
1. **Per-Preset Modulation Matrix Routes:** Highly flexible, saved in presets, can target any parameter.
2. **Global Shell Pad Pressure Layer:** Fast, instant filter opening across any sound, toggled from the Master page.

```
                    +-----------------------------+
                    |  MPC Pad Pressure (0xA0)    |
                    +--------------+--------------+
                                   |
         +-------------------------+-------------------------+
         |                                                   |
         v                                                   v
+-------------------------------+           +-------------------------------+
| 1. Preset Modulation Matrix   |           | 2. Global Shell Layer         |
| - Source: Poly Aftertouch     |           | - Master: PAD PRESS switch    |
| - Targets: ANY parameter      |           | - Master: PAD % knob (0-100%) |
| - Stored in preset / project  |           | - Targets Filter 1 Cutoff ONLY|
| - Polyphonic per-voice control|           | - Smoothed with 12 ms filter  |
+---------------+---------------+           +---------------+---------------+
                |                                           |
                |   Does preset already route               |
                |   Poly Aftertouch -> Filter 1 Cutoff?     |
                |                 |                         |
                +-------- YES ----+-----> [ SUPPRESSED ] <--+
                |                         (De-duplication: no doubling)
                v                                   v
+-------------------------------+           +-------------------------------+
| Voice Filter 1 Cutoff Offset  | <---------+ Adds up to +36 semitones      |
+-------------------------------+           +-------------------------------+
```

### 5.1 Per-Preset Modulation Matrix Routes

The modulation matrix allows routing **Poly Aftertouch** (source choice 38) to any of SynthyBoy's modulatable destinations:
- **Per-voice polyphony:** Each held pad modulates only its own voice. You can hold a three-note chord and push harder on just the top note to open its filter or introduce vibrato, while the lower two notes remain completely unaffected.
- **Stored in presets:** Modulation matrix routings are saved within the `.sbpreset` file and in the DAW/MPC project chunk.

### 5.2 Global Shell Pad Pressure Layer (`PAD PRESS` and `PAD %`)

For quick jamming without editing the modulation matrix, SynthyBoy includes a built-in shell pressure layer ([docs/MPC.md, "Pad pressure"](../MPC.md#pad-pressure); [shell-mpc/pad_pressure.h](../../shell-mpc/pad_pressure.h)):
- **Controls on Master Page (Main side, VOICES block):**
  - **PAD PRESS:** Toggle switch (parameter index 2673, `Pad Pressure`, values `Off` / `On`). Default is **OFF**.
  - **PAD %:** Amount knob (parameter index 2674, `Pad Pressure Amount`, range 0 to 100%). Default is **35%** (providing approximately one octave of cutoff opening at full pressure).
- **Target:** Modulates the cutoff of **Filter 1 ONLY** (not Filter 2, oscillators, or effects).
- **Depth:** Up to +36 semitones of cutoff offset at 100% amount and maximum pressure.
- **Smoothing:** Pressure values are smoothed using a one-pole low-pass filter with a 12 ms time constant (`core/engine-own/voice/voice.cpp:704`) to eliminate stepping and zipper noise.
- **Automatic De-duplication:** If a preset's modulation matrix already contains an explicit route from `Poly Aftertouch` to `Filter 1 Cutoff` (such as factory lead presets), the global layer automatically suppresses itself for that voice (`core/engine-own/voice/voice.cpp:706`). This guarantees that presets designed with custom filter pressure curves do not suffer from doubled modulation or runaway brightness.
- **State storage:** Saved in the project chunk tail (`mpcPadPressure` and `mpcPadPressureAmount`), independent of preset data.

---

## 6. How Factory Presets Use Pad Pressure

Of the 136 factory presets included with SynthyBoy (112 initial presets plus 24 from sound set batch 1), **65 presets** carry an integrated `Poly Aftertouch -> Filter 1 Cutoff` route in their modulation matrix (and 66 presets in total route Poly Aftertouch; [scripts/factory-preset-pad-pressure.json](../../scripts/factory-preset-pad-pressure.json); [docs/MPC.md](../MPC.md)):

### Preset Categories and Calibration Amounts

1. **Drones & Ambient Textures (30% to 45%):**
   - *Bode Shifter Drone* (30%), *Dark Cinema Drone* (45%), *Deep Sea Ambient* (45%), *Frozen Aurora* (45%), *Harmonic Cloud* (45%), *Infinite Horizon* (45%), *Slow Motion Waves* (45%), *Space Odyssey Pad* (45%), *Shepard Ascent* (35%), *Lorenz Garden* (40%), *Breath of Machines* (35%).
   - *Behavior:* Sustained drone beds begin dark and atmospheric; digging into a pad opens upper harmonics and sweeps the frequency spectrum without re-triggering attack envelopes.
2. **Organs & Keys (18% to 70%):**
   - *Accordion Reed* (25%), *B3 Drawbar Organ* (25%), *FM Soft Organ* (25%), *Glass Organ Tones* (25%), *Vibraphone Bars* (25%), *Rotary Rock Organ* (70%), *Tempo Gate Chords* (18%), *Wah Pressure Clav* (46%).
   - *Behavior:* Simulates swell pedals or keybed pressure, driving organ drawbar overtones and emulating rotary speaker saturation.
3. **Leads & Solos (14% to 70%):**
   - *Bell Chime Lead* (30%), *Chorused Saw Solo* (30%), *Cosmic Drift Lead* (30%), *Diode Bite Lead* (30%), *Dirty Peak Lead* (30%), *Dual Octave Hero* (30%), *FM Digital Lead* (30%), *Harmonic Shimmer Lead* (30%), *Hyper Saw Lead* (70%), *Laser Beam Lead* (30%), *Metallic Edge Lead* (30%), *Phaser Sweep Lead* (30%), *Pulse Width Mod Lead* (30%), *Screaming Sync Lead* (30%), *Solo Square Lead* (30%), *Tri Soft Solo* (30%), *Twin Warp Lead* (30%), *Vocaloid Lead* (30%), *Warm Vintage Lead* (30%), *MPE Pressure Lead* (55% Cutoff, +20% Unison Detune), *Sync Cry Lead* (26%), *Triple Fifth Lead* (24%), *Vowel Whistle Lead* (18%), *Bell FM Ripple Lead* (14%).
   - *Behavior:* Solo melodies can be dynamically accented note-by-note; pressing hard expands filter brightness and increases supersaw detuning on sustained apex notes.
4. **Pads, Strings & Choirs (40% to 70%):**
   - *Cathedral Choir* (45%), *Comb Reso Swell* (45%), *Diffusor Cloud Pad* (40%), *Ethereal Glass* (45%), *Gentle Flute Pad* (40%), *Golden Dawn Pad* (45%), *Harmonic Cloud* (45%), *Hyper Dimension Pad* (40%), *Lush Analog Pad* (70%), *Odd Harmonics Pad* (40%), *Radiant Pulse Pad* (40%), *Starlight Shimmer* (40%), *Subtle Tape Drift* (40%), *Tri-Band Split Texture* (40%), *Velvet Strings* (40%), *Vintage Brass Pad* (70%), *Vowel Choir Pad* (45%), *Bowed Pressure Strings* (46%), *Brass Swell Ensemble* (45%), *Mid Side Halo* (40%), *Parallel Dusk Pad* (40%), *Stretched Glass Choir* (42%).
   - *Behavior:* Warm string and choir swells expand naturally in volume and presence under finger pressure, breathing with the performer's touch.
5. **Bass (18%):**
   - *Subterranean Duo* (18%).
   - *Behavior:* Sustained sub bass holds solid at the bottom while finger pressure opens upper saw bite and filter presence on held notes.

---

## 7. Step-by-Step Guide: Routing Pad Pressure on the Mod Page

Follow these steps on the MPC standalone touchscreen to route pad pressure to any parameter in SynthyBoy:

```
+-----------------------------------------------------------------------+
| MODULATION MATRIX                                          [ + ADD ]  |
| Tabs: [ ALL ]  [ BY SOURCE ]  [ BY DEST ]                             |
+-----------------------------------------------------------------------+
| Empty Matrix View: 57-Source Quick-Add Board (ModSourceBoard)         |
| [ ENVELOPES ]  Env 1   Env 2   Env 3   Env 4   Env 5   Env 6          |
| [ LFOS ]       LFO 1..10   RLFO 1..4                                  |
| [ MACROS ]     Macro 1..8                                             |
| [ PLAYER ]     Velocity  Note  Aftertouch  PitchWhl  ModWhl  Random   |
|                Lift  Slide  OctNote  >>> [ POLY AFTERTOUCH ] <<<      |
| [ SOUND ]      Osc A..C  Noise  Sub  Filter 1..2  ActiveVoices ...    |
+-----------------------------------------------------------------------+
| Populated Matrix View: Row Structure (72 px)                          |
| [SRC: Poly Aftertouch]  [DST: Filter 1 Cutoff]  [DEPTH: ====o  +55%]  |
| Q1: Select Row   | Q2: Change Source | Q3: Change Target | Q4: Depth  |
+-----------------------------------------------------------------------+
```

### Step 1: Open the Mod Page
- Press the physical **Mod** function key on the hardware, or tap the **Mod** tab on the MPC touchscreen.

### Step 2: Select the Source
- **If the matrix has 0 active slots:** SynthyBoy displays the 57-source Quick-Add board (`ModSourceBoard`) ([docs/MPC.md, line 1138](../MPC.md#mod-matrix-and-macros)). Tap the **POLY AFTERTOUCH** chip located in the **PLAYER** category.
- **If routes already exist:** Tap **+ ADD** at the top right. In the newly created row, tap the **SOURCE** button (or turn Q-Link knob **Q2**). In the source picker list, select **Poly Aftertouch** (source choice 38).

### Step 3: Choose the Destination Target
- Tapping the source chip or the **TARGET** column opens the grouped destination picker (`ModTargetsPage`).
- Browse categories (Oscillators, Filters, Envelopes, LFOs, Mixer, FX, Voice Control) and tap your desired parameter (e.g., `Filter 1 Cutoff`, `Osc 1 WT Pos`, `Delay Send`).

### Step 4: Adjust Modulation Depth
- Drag the depth slider bar horizontally on the touchscreen, or turn Q-Link knob **Q4 (AMOUNT)**.
- *Polarity:* **Poly Aftertouch** is a unipolar source (`0.0` at rest, `1.0` at maximum pressure). A positive depth increases the target parameter above its current knob position; a negative depth decreases it towards minimum.

### Step 5: Advanced Slot Detail (Optional)
- Tap the **MORE** button or double-tap the slot row to open the **Slot Detail Page**:
  - **Auxiliary Source:** Multiply pad pressure by a second source (e.g., scale pressure by `Mod Wheel` or `Env 2`).
  - **Curve / Shape:** Bend the response curve to require harder finger pressure before modulation kicks in, or make it hypersensitive to gentle touch.
  - **Rise / Fall Smoothing:** Adjust slew rate times if you desire slower cinematic transitions rather than instantaneous pad response.

---

## 8. Five Concrete Playing Recipes

Here are five production-ready modulation recipes tailored specifically for MPCe pads. For each recipe, load an initialized or basic patch, set the base parameters, and dial in the modulation matrix slots.

---

### Recipe 1: Bass — "Growl & Bite Punch"

A dynamic synth bass that delivers a solid, punchy foundation under normal playing, but screams with aggressive analog resonance and saturation when you lean into the pad.

- **Modulation Slot 1:**
  - **Source:** `Poly Aftertouch` (choice 38)
  - **Target:** `Filter 1 Resonance`
  - **Amount:** `+45%`
- **Modulation Slot 2:**
  - **Source:** `Poly Aftertouch` (choice 38)
  - **Target:** `Distortion Drive` (FX Slot 1: Distortion)
  - **Amount:** `+30%`
- **Base Patch Settings:**
  - Oscillator A: Saw wave, 1 voice.
  - Filter 1: 24 dB Low Pass, Cutoff set low (~350 Hz), Resonance low (15%).
  - Env 1 (Amp): Attack 0 ms, Decay 450 ms, Sustain 40%, Release 120 ms.
- **What You Hear:**
  Normal rhythmic bass lines retain a clean, tight, punchy low-end groove. When you hold a bass note and press down firmly, the filter resonance spikes into screaming aggression and drives the distortion circuit into crunchy overtones—without retriggering the note or changing its pitch.

---

### Recipe 2: Lead — "Singing Vocal Swell & Supersaw Detune"

A expressive solo lead where finger pressure controls both filter brightness and stereo unison width, allowing dramatic musical climaxes on sustained notes.

- **Modulation Slot 1:**
  - **Source:** `Poly Aftertouch` (choice 38)
  - **Target:** `Filter 1 Cutoff`
  - **Amount:** `+50%`
- **Modulation Slot 2:**
  - **Source:** `Poly Aftertouch` (choice 38)
  - **Target:** `Osc 1 Unison Detune`
  - **Amount:** `+35%`
- **Modulation Slot 3:**
  - **Source:** `Poly Aftertouch` (choice 38)
  - **Target:** `Osc 1 Morph`
  - **Amount:** `+40%`
- **Base Patch Settings:**
  - Oscillator A: Wavetable with vocal or saw harmonics, Unison set to 5 voices, Detune knob set to a modest 10%.
  - Filter 1: 12 dB Low Pass, Cutoff at 1.2 kHz.
- **What You Hear:**
  Fast melodic passages sound crisp, focused, and articulate. Lingering on a sustained note and bearing down with your finger dramatically sweeps the filter open, morphs the wavetable into an open vocal vowel, and widens the unison voices into a massive, lush supersaw halo.

---

### Recipe 3: Pad — "Ethereal Polyphonic Crystalline Swell"

A warm ambient chord pad that demonstrates true per-voice polyphonic aftertouch: pressing down on a single note in a chord makes only that note shimmer into digital brightness and wash into reverb, while the other notes remain dark and dry.

- **Modulation Slot 1:**
  - **Source:** `Poly Aftertouch` (choice 38)
  - **Target:** `Osc 1 Frame` (Wavetable Position)
  - **Amount:** `+60%`
- **Modulation Slot 2:**
  - **Source:** `Poly Aftertouch` (choice 38)
  - **Target:** `Reverb Send` (or Reverb Mix)
  - **Amount:** `+35%`
- **Base Patch Settings:**
  - Oscillator A: Wavetable that transitions from warm analog saw at Frame 0 to crystalline bells/glass at Frame 100. WT Pos knob at 0.
  - Reverb: Decay 4.5 s, Mix at 20%.
  - Filter 1: Warm 24 dB Ladder Low Pass, Cutoff at 900 Hz.
- **What You Hear:**
  Play and hold a four-note chord. All notes sound warm, dark, and intimate. While holding the chord, press down firmly with your ring finger on the top melody note: only that single note glides into shimmering crystalline harmonics and dissolves into cavernous reverb space. The bass and middle harmony notes stay completely warm and unaffected underneath.

---

### Recipe 4: Pluck — "Damped Pizzicato to Resonant Ring"

A versatile acoustic-style pluck sound that shifts seamlessly between short, muted pizzicato attacks and long, singing, bell-like rings depending on how you rest into the pad.

- **Modulation Slot 1:**
  - **Source:** `Poly Aftertouch` (choice 38)
  - **Target:** `Env 1 Decay`
  - **Amount:** `+50%`
- **Modulation Slot 2:**
  - **Source:** `Poly Aftertouch` (choice 38)
  - **Target:** `Filter 1 Cutoff`
  - **Amount:** `+40%`
- **Base Patch Settings:**
  - Oscillator A: Short acoustic pluck or triangle/pulse wavetable.
  - Filter 1: 24 dB Low Pass, Cutoff at 600 Hz.
  - Env 1: Attack 0 ms, Decay 160 ms, Sustain 0%, Release 100 ms.
- **What You Hear:**
  Tapping the pads with quick release produces tight, percussive, muted kalimba/pizzicato plucks. If you strike and hold down firmly into the pad, the decay time instantly lengthens to over 2 seconds and the filter opens, transforming the strike into a long, resonant chime that rings out musically.

---

### Recipe 5: FX — "Runaway Feedback Vortex & Tape Flutter"

An experimental performance effect where pad pressure pushes an echo buffer into runaway analog self-oscillation while introducing dizzying pitch flutter.

- **Modulation Slot 1:**
  - **Source:** `Poly Aftertouch` (choice 38)
  - **Target:** `Delay Feedback` (FX Delay)
  - **Amount:** `+55%`
- **Modulation Slot 2:**
  - **Source:** `Poly Aftertouch` (choice 38)
  - **Target:** `LFO 1 Rate`
  - **Amount:** `+45%`
- **Modulation Slot 3:**
  - **Source:** `Poly Aftertouch` (choice 38)
  - **Target:** `Mod 01 Amount` (Slot 1: LFO 1 -> Pitch / Delay Offset)
  - **Amount:** `+40%`
- **Base Patch Settings:**
  - FX Rack: Delay enabled, Time 3/16, Feedback 30%, Mix 40%.
  - LFO 1: Sine wave, Rate 1.5 Hz, routed to Oscillator Pitch or Delay Time offset in Slot 1.
- **What You Hear:**
  Under normal playing, notes produce standard rhythmic, clean stereo repeats. Digging hard into the pad immediately pushes the delay feedback past 100% into roaring analog tape self-oscillation while simultaneously accelerating the pitch flutter rate. Easing finger pressure instantly pulls the delay back below unity, letting the feedback wash decay naturally.

---

## 9. Generation 1 vs Generation 2 Comparison

| Feature / Capability | First Generation (MPC Key 61, Live I/II, One/One+, MPC X) | Second Generation (MPC XL, MPC Live III) |
|---|---|---|
| **Pad Hardware Type** | Standard thick rubber pads [Standalone OS Guide v3.10.1, p. 62] | MPCe 3D expressive pads [User Guide v3.10.1, p. 4, 11] |
| **Strike Velocity** | Yes (0–127) | Yes (0–127) |
| **Pad Pressure Sensor** | Basic velocity/aftertouch sensor [Standalone Guide, p. 333] | High-resolution multi-axis continuous pressure sensor [User Guide v3.10.1, p. 64, 350] |
| **Pad Pressure Rate to Plugin** | Intermittent / lower rate | High-speed continuous stream (~170 Hz / every 6 ms) ([docs/MPC.md](../MPC.md#mpc-xl-pads)) |
| **Polyphonic Aftertouch (`0xA0`)** | Supported if configured in pad preferences | Supported natively per held pad |
| **Channel Aftertouch (`0xD0`)** | Sent by keyboard keybeds (e.g. MPC Key 61, 61 keys) [Standalone Guide, p. 369] | Not sent by pads (pads send `0xA0`) |
| **Finger X/Y Coordinate Sensing** | **None** | Hardware senses X, Y, Center Dist, Press Dist, dX, dY (internal Drum Tracks only) [MPC 3.6, p. 2] |
| **Pad Quadrant Sensing (4 Corners)** | **None** | Yes (drum articulations, layer triggering, Tune/Mix component) [User Guide v3.10.1, p. 13, 225, 234] |
| **8×8 (64 Pad) Performance Mode** | **None** (4×4 grid only) | Yes (split Banks A–D or E–H for 64 playable pads) [User Guide v3.10.1, p. 72; MPC 3.10, p. 4-5] |
| **MPCe Q-Link Mode** | **None** | Yes (XY touch surface on Drum tracks) [User Guide v3.10.1, p. 337, 350] |
| **X/Y Transmitted to VST Plugins** | No | **No** (host limitation; measured on MPC XL) ([docs/MPC.md](../MPC.md#mpc-xl-pads)) |
| **SynthyBoy Mod Source: Velocity** | Active (Source 23) | Active (Source 23) |
| **SynthyBoy Mod Source: Poly Aftertouch** | Active (Source 38) | **Active (Source 38) — Primary Expressive Dimension** |
| **SynthyBoy Global PAD PRESS Layer** | Active (Filter 1 Cutoff, Master page) | Active (Filter 1 Cutoff, Master page) |

---

## 10. To Confirm on the Device

To adhere strictly to engineering rigor, the following behaviors could not be definitively proven from the published Akai manuals or automated developer test rigs and remain to be confirmed on physical hardware:

1. **Host MPCe Q-Link Project Mode to Plugin Automation:**
   - *Manual Statement:* The MPC manual states that MPCe Q-Link Mode is accessible on Drum Tracks, but offers both "Track" and "Project" modes ([MPC 3.7 Update Release Notes, p. 2]; [User Guide v3.10.1, p. 350]).
   - *Question:* Can an MPCe pad on a Drum Track be assigned via Q-Link Learn in "Project" mode to automate a SynthyBoy VST parameter (e.g. Filter Cutoff or Wavetable Frame) using its X or Y axis while a sequence is playing?
2. **Pad Perform Mode with Poly Aftertouch on Plugin Tracks:**
   - *Manual Statement:* Pad Perform offers scale and chord mapping across pads and includes a "Use Aftertouch" toggle [User Guide v3.10.1, p. 83].
   - *Question:* When Pad Perform (Notes, Chords, or Progressions) is active on a Plugin Track, does the host preserve polyphonic aftertouch (`0xA0`) for each sounding note in a chord, or does it collapse them into global channel aftertouch or drop pressure altogether?
3. **8×8 Notes Mode Poly Aftertouch Behavior:**
   - *Manual Statement:* In 8×8 mode, the 16 physical pads are split into 64 sub-zones across four banks [MPC 3.10 Release Notes, p. 4-5].
   - *Question:* When playing melodic lines in 8×8 mode on a plugin track, does each sub-quadrant independently transmit continuous polyphonic aftertouch, and is pressure sensitivity uniform across the quadrant edges?
4. **Note Repeat and Arpeggiator Pressure Interaction:**
   - *Manual Statement:* Note Repeat and Arpeggiator feature "Aftertouch to Velocity" settings [Standalone OS Guide, p. 104; User Guide v3.10.1, p. 102].
   - *Question:* When Note Repeat is held down on a pad triggering SynthyBoy, does the host send continuous aftertouch streams (`0xA0`) alongside repeated note-ons, or does it only adjust the note-on velocity of subsequent repeats?
5. **Release Velocity / Lift Transmission:**
   - *Question:* Do MPCe pads transmit genuine release velocity in Note-Off events (`0x80`) to VST plugins when fingers are lifted rapidly, or is Note-Off velocity fixed at 0 or 64 by the host?
6. **Hardware Pad Curves vs Pressure Slew Rate:**
   - *Question:* Does the hardware **Pad Curve** setting (A, B, C, D in Preferences > Hardware) [User Guide v3.10.1, p. 63] reshape only the initial note-on strike velocity, or does it also alter the response transfer function of continuous polyphonic aftertouch streamed to external plugins?
