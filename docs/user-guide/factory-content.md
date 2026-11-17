# SynthyBoy Factory Content Pack Guide

This chapter details the factory content pack included with SynthyBoy, covering the complete library of wavetables, samples, multisample instruments, and impulse responses, how to load and browse content on the oscillator screens, engine fallback behavior, and licensing terms.

---

## 1. Overview & Content License

The SynthyBoy factory content pack provides **11.5 MB (54 files)** of custom-synthesized acoustic and electronic sound assets, optimized specifically for the Akai MPC hardware platform:

- **Factory Wavetables**: 16 wavetables (3.8 MB) covering formant sweeps, pulse-width modulation stacks, additive bells, FM harmonic series, tonewheel drawbars, and wavefolders.
- **Factory Samples**: 12 audio samples (44.1 kHz, 16-bit PCM) including sustained bowed strings, breath noise chiffs, chord progressions, drum breaks, crystal glass, metal gongs, acoustic marimbas, plucks, rising risers, vinyl noise beds, and vocal sweeps.
- **Multisample Instruments**: 3 key-mapped SFZ instruments with multi-zone recordings and velocity layer switching.
- **Impulse Responses**: 3 stereo 24-bit impulse responses for the zero-latency Convolve effect (guitar amp cabinet, studio plate, acoustic small room).

### License & Attribution

All content included in the factory content pack is generated algorithmically from mathematical equations and physical modeling synthesis using `scripts/gen-content-pack.py`. It contains **no third-party samples, copyrighted audio recordings, or proprietary materials**.

As declared in the generator scripts and package manifests (`contentpack.json`):

> **License:** Creative Commons Zero v1.0 Universal (`CC0-1.0`)  
> **Attribution:** Safian Szabolcs  
> **Specification:** Synthesized from nothing (CC0-1.0, no third-party material).

You are completely free to use all factory wavetables, samples, instruments, presets, and impulse responses in commercial music releases, film scores, broadcast productions, sound design projects, and live performances with no royalty obligations or licensing restrictions.

---

## 2. Factory Wavetables (`com.safian.synthyboy.factory-wavetables`)

The factory wavetables appear in the wavetable browser under the categories of the pack (the folders of the pack, or the `category` of the manifest). The wavetable pack contains **16 wavetables** formatted as 16-bit mono PCM WAV files at 2048 samples per single-cycle frame (up to 64 frames per table), conforming to the format read by `core/engine-own/osc/content_set.cpp`.

They are designed for the **Wavetable oscillator** (Osc A, B, C) and can also serve as spectral filter tables in the **Spectral oscillator**.

| File Name | Frames | Size | Sonic Character & Musical Applications |
|---|---|---|---|
| `additive_bell_strike.wav` | 64 | 256 KB | Struck acoustic bell partials with high overtones decaying naturally as frames advance; ideal for celestas, chimes, and FM tines. |
| `additive_glass_harmonica.wav` | 48 | 192 KB | Sparse, pure sinusoidal partial series that open progressively one by one; perfect for glass harmonicas, organs, and airy flutes. |
| `choir_singers_shift.wav` | 48 | 192 KB | Vowels A, O, and U modeled through male, female, and child vocal tracts; excels at evolving vocal pads and sacred choirs. |
| `fm_feedback_saw.wav` | 64 | 256 KB | Single FM operator with self-feedback sweeping smoothly from 0 to 1.9 (sine to saw morph); ideal for dynamic modern leads and basses. |
| `fm_index_sweep.wav` | 64 | 256 KB | 2:1 ratio frequency modulation with modulation index sweeping from 0 to 9; delivers cutting digital bite and classic 80s FM keys. |
| `fm_ratio_ladder.wav` | 64 | 256 KB | Steps through integer modulator ratios 1 to 8 (8 frames per ratio) with rising index; tailor-made for snappy FM basslines and electro leads. |
| `formant_sweep_aeiou.wav` | 64 | 256 KB | Speech vowels A, E, I, O, U filtered through vocal formants at 220 Hz; essential for talking dubstep basses and vocaloid leads. |
| `harmonic_sieve.wav` | 32 | 128 KB | Sawtooth wave filtered to contain only every n-th harmonic (n = 1..16); produces hollow pipe organs and comb-filtered textures. |
| `metal_comb_sweep.wav` | 64 | 256 KB | Sharp resonant comb filter sweeping through a dense saw spectrum; creates flanged synth leads and physical string resonances. |
| `organ_drawbars_16.wav` | 31 | 124 KB | 16 distinct tonewheel drawbar registrations (fundamental at 16-foot, transpose +12); covers jazz, gospel, and rock organs. |
| `overtone_throat_sweep.wav` | 64 | 256 KB | Narrow bandpass resonance walking up and down the 4th through 20th partials; mimics Tuvan overtone singing and whistling drones. |
| `pwm_dual_phase.wav` | 64 | 256 KB | Two opposite sawtooth waves whose phase distance breathes from 8% to 50% to 8%; foundational for rolling Reese basses and PWM strings. |
| `pwm_saw_blend.wav` | 64 | 256 KB | Pulse wave sweeping width from 50% down to 4% while a saw wave fades in; classic 80s analog polysynths and evolving brass. |
| `pwm_triple_stack.wav` | 64 | 256 KB | Three stacked pulse waves with wandering independent pulse widths; delivers massive, chorused analog pulse leads and pads. |
| `ring_mod_ladder.wav` | 64 | 256 KB | Ring modulation stepping through integer carrier ratios 2 to 17; perfect for inharmonic metallic strikes and sci-fi bell tones. |
| `sine_fold_sweep.wav` | 64 | 256 KB | Pure sine wave driven through a wavefolder with gain expanding from 0.4 to 14; essential for west-coast Buchla tones and snarling basses. |

---

## 3. Factory Samples (`com.safian.synthyboy.factory-samples`)

The sample pack includes **12 audio recordings/textures**, **3 multisample instruments**, and **3 impulse responses** recorded at 44.1 kHz, peak-normalized to -1 dBFS headroom with embedded `smpl` metadata.

### Audio Samples (12 Files)

| File Name | Length | Ch | Target Oscillator & Usage | Description & Sound Character |
|---|---|---|---|---|
| `bowed_tone.wav` | 3.15 s | 1 | **Sample / Granular / Spectral** | Bowed string tone tuned to MIDI 60 with 5 Hz natural vibrato and seamless loop points (14.3%..77.7%); delivers expressive solo strings and sustained orchestral pads. |
| `breath_noise_burst.wav` | 1.20 s | 1 | **Sample / Granular** | Three resonant bands of acoustic breath noise; provides realistic wind transients (flute chiffs, pipe organ air) on non-legato note attacks. |
| `chord_pad_sections.wav` | 8.00 s | 1 | **Granular / Spectral** | Evolving 4-chord harmonic progression (Cmaj7, Amin7, Fmaj7, G6; 2 seconds each, crossfaded); ideal for ambient chord clouds and spectral morphing. |
| `drum_loop_120bpm.wav` | 4.00 s | 1 | **Sample (Slice) / Granular** | 2-bar 4/4 acoustic drum break at 120 BPM (kick, snare, ghost snares, closed and open hats); sliced across 32 keys in Slice mode or glitched via granular playback. |
| `glass_ping.wav` | 3.00 s | 1 | **Sample / Granular / Spectral** | Struck crystal wine glass at MIDI 72 with six inharmonic partials and natural acoustic beating; creates tuned wine glass harps and crystalline plucks. |
| `gong_metal.wav` | 4.00 s | 1 | **Sample / Granular / Spectral** | Deep metal gong tuned to MIDI 36 with 13 inharmonic modes and high-frequency sizzle; used for backward crescendos and granular metal swarms. |
| `mallet_marimba.wav` | 1.80 s | 1 | **Sample / Granular** | Single struck marimba bar tuned to MIDI 60 with physical acoustic ratios (1 : 3.99 : 10.1); delivers round wooden percussive plucks. |
| `pluck_string.wav` | 2.50 s | 1 | **Sample / Granular** | Additive acoustic plucked string with natural high-frequency decay, tuned to MIDI 60; ideal for harp plucks and granular frozen string drones. |
| `rising_texture.wav` | 6.00 s | 1 | **Granular / Sample** | 6-second tension riser featuring noise band climbing 250 Hz to 7 kHz and a 2-octave gliding harmonic stack; builds tension before drops. |
| `vinyl_noise_bed.wav` | 4.00 s | 2 | **Sample (Texture)** | Continuous stereo vinyl record noise with hiss, turntable rumble, 50 Hz electrical hum, crackles, and pops; adds vintage warmth under keys and pads. |
| `voice_sweep_aeiou.wav` | 5.00 s | 1 | **Granular / Spectral** | Singing voice at 130.8 Hz (MIDI 48) articulating A, E, I, O, U over 5 seconds with natural vibrato and breath; scrubbed via mod wheel or granular playhead. |
| `vowel_pad.wav` | 4.10 s | 1 | **Spectral / Granular / Sample** | Three voices singing sustained A and O choral vowels at MIDI 60 with seamless vibrato loop; creates lush sacred choirs and spectral freezes. |

### Multisample SFZ Instruments (3 Instruments)

The Multisample oscillator (`oscN.type` Multisample) loads key-mapped instruments formatted as `.sfz` files with associated audio sample folders:

| Instrument | Zones | Velocity Layers | Files | Sound Character & Playing Tips |
|---|---|---|---|---|
| `pluck_nylon` | 6 key zones | 1 layer | 6 WAVs | Acoustic nylon-string guitar recorded across six keyboard zones (roots MIDI 36, 45, 54, 63, 72, 81); responds with authentic guitar body resonance. |
| `mallet_marimba` | 5 key zones | 2 layers | 10 WAVs | Acoustic marimba with dynamic velocity switching: soft playing (vel 1..84) triggers round, dark wooden bars; hard strikes (vel 85..127) trigger sharp mallet knock. |
| `glass_bells` | 4 key zones | 1 layer | 4 WAVs | Struck wine glass samples mapped across four octave registers (roots MIDI 48, 60, 72, 84); delivers ethereal tuned glass bells. |

### Impulse Responses for Convolve (3 Files)

The **Convolve** effect slot incorporates zero-latency partition convolution. All impulse responses are stereo, 24-bit PCM at 44.1 kHz, normalized to an L2-norm of 1.0 (so that wet signal level matches dry signal level at `convolve.ir_gain` 0 dB):

| Impulse Response | Length | Size | Sound Character & Recommended Preset Use |
|---|---|---|---|
| `cabinet_colour.wav` | 0.09 s (90 ms) | 23 KB | Open-back guitar amplifier cabinet profile (low-end roll-off below 70 Hz, 95 Hz cabinet resonance thump, dual-mic presence peaks at 2.5 kHz and 3.3 kHz, 5.2 kHz lowpass roll-off); adds warm speaker coloration to electric pianos and basslines. |
| `plate.wav` | 1.45 s | 375 KB | Classic bright studio plate reverb with instant high-density diffusion, RT60 of 1.25 seconds, and a high-frequency sheen decaying in 0.35 seconds; provides sparkling space for vocals, strings, and bells. |
| `small_room.wav` | 0.70 s | 181 KB | Intimate acoustic room with eight distinct early reflections in the first 46 milliseconds and a smooth darkening 0.42 s reverberant tail; glues drum breaks and acoustic mallets into a realistic physical space. |

---

## 4. How to Browse and Load Content on Oscillator Screens

SynthyBoy provides a unified **Content Browser** accessible directly from the touchscreen UI (overlay build) on the **Osc A**, **Osc B**, and **Osc C** pages.

### Loading Wavetables (Wavetable Oscillator): the wavetable browser
1. Select the oscillator tab (**Osc A**, **Osc B**, or **Osc C**) and set the type to **WAVE**.
2. Tap the name in the table selector (or its list button). It works on the Osc A / B / C pages and on the oscillator third of the Play page (which opens the full screen first).
3. The wavetable browser opens as a list over the screen. It opens in the **category** of the table you are playing, with that table marked. The bar at the top is the category name with a back arrow: tap it to see all categories (**Basic**, **Morph**, the categories of the installed packs such as Analog or Vocal, the folders you made yourself, **User**), tap a category to see its tables.
4. Tap a table to play it. A built-in table is selected at once. A table from a pack or from your own files is read from the card or the drive, and the amber bar at the top of the page shows while that happens; the old table keeps playing until the new one is ready. The selector then shows the name of the table.
5. A table is made of **frames**; scan through them with **WT POS** (the frame position). Tables with one frame are single cycles.
6. **Stepping**: with a built-in table the arrows of the selector (and the Q knob that is on it) step through the 15 built-in tables. With a table from a file the arrows and the knob step to the **previous / next table of the same category**, so you can audition a whole category with the knob. At the first and the last table of the category nothing happens (no wrap). To go back to the built-in tables, open the browser.
7. A file that cannot be used (too large, empty, not a table) says why in its row ("not a wavetable file: ...") and the display shows the same for a few seconds; the sound does not change.

### Adding your own wavetables
Copy WAV files to the folder **`SynthyBoy/Wavetables/`** at the top level of the **SD card** (`/sdcard/SynthyBoy/Wavetables/`), of a **USB drive** (`<drive>/SynthyBoy/Wavetables/`), or inside the plugin's own **`Content`** folder (`Content/SynthyBoy/Wavetables/` next to the VST folder; the way to add files on a machine without a card). Create the folders if they are not there.

- A file directly in `SynthyBoy/Wavetables/` is listed in the category **User**.
- A folder inside it is a **category** named like the folder, and its WAV files are the tables of it (one folder level; files in deeper folders are not listed). The same folder name on the card and on a drive is one category.
- There is no index to build and no restart: the browser lists what is there **each time it opens**.
- A presets that uses one of your files remembers it by folder and name (and a checksum). On another device the same file under the same folder name makes it sound the same; without the file the oscillator plays a **saw** and the display says "content missing: <name>".
- Which files work: WAV, **mono** (a stereo file uses its left channel), 16, 24 or 32 bit integer or 32 bit float, up to **16 MB**. The usual "wavetable WAV" is **N frames of 2048 samples one after the other** (up to 256 frames; more are cut off). A file with a `clm` chunk (the Serum style "<!>1024 ..." chunk) uses the frame size in the chunk (1024, 4096 and so on are resampled to 2048). A file of 16 to 4096 samples whose length is not a multiple of 2048 is one **single cycle** and is stretched to one frame. Anything else (a long recording, 8 bit, compressed WAV, FLAC, MP3) is not a table and is refused with a reason.
- A **pack** (folder with a `contentpack.json` of kind `wavetables` in `Content/packs/<pack>/`) is listed too: the files are grouped by the `category` of the manifest entry or, without one, by the folder they are in; `title` gives the shown name.

### Loading Samples, Grains, and Spectra (Sample, Granular, Spectral Oscillators)
1. Set the oscillator type to **SAMPLE** (Type 1), **GRAIN** (Type 2), or **SPEC** (Type 3).
2. If no file is loaded, a prominent **LOAD SAMPLE** button appears over the display area. If a file is already loaded, the source stepper displays the filename.
3. Tap **LOAD SAMPLE** or tap the source name/dropdown to open the **Content Browser**:
   - **Header Bar**: Displays the current directory path. Tapping the up-arrow button (`..`) navigates up to the parent directory.
   - **Root Directories**: Tapping the top-level roots takes you directly to:
     - **PLUGIN CONTENT**: Internal factory packs (`Content/packs/`).
     - **SD CARD**: Hardware SD card mount (`/sdcard`).
     - **USB <NAME>**: Any external USB drives or thumb sticks mounted under `/media`.
   - **File Filtering**: The browser automatically displays only compatible `.wav` audio files. Incompatible formats (FLAC, MP3, TXT) are omitted.
4. Tap any file to load it. The display thread loads and decodes the file asynchronously in the background. The audio thread never waits, ensuring completely glitch-free playback.
5. The display updates with the waveform overview, loop points, and real-time playhead indicators.

### Loading Multisample SFZ Instruments
1. Set the oscillator type to **MULTI** (Type 4).
2. If empty, the display presents a **LOAD INSTRUMENT** button.
3. Tap the button to open the Content Browser. Navigate into `instruments/` within `PLUGIN CONTENT` or your external drive.
4. Select any `.sfz` file. SynthyBoy parses the key zones, root notes, and velocity ranges.
5. Alternatively, selecting a single `.wav` file automatically creates a single-zone instrument mapped across the entire 128-note MIDI range.

---

## 5. Engine Fallback Behavior When Files Are Missing

SynthyBoy is designed with robust error-handling to prevent plugin crashes or frozen audio threads when referenced content files cannot be located (for instance, when opening a project on an MPC without external content packs):

1. **Missing Wavetables**: If a preset references a factory or user wavetable that cannot be loaded or fails SHA-256 validation, the engine automatically falls back to a clean **sawtooth** waveform.
2. **Missing Sample, Granular, Spectral, or Multisample Files**: In accordance with the engine specification ("no fallback tone"), an unresolvable sample file causes that specific oscillator to remain **silent** rather than emitting an unpredictable harsh tone.
3. **Missing Convolve Impulse Responses**: If an impulse response WAV cannot be located, the Convolve module bypasses convolution and passes the **dry input signal** cleanly through to subsequent effect slots.
4. **Dual-Layer Factory Sound Design**: To ensure that all Sound Set Batch 2 presets remain auditionable and musically useful even without the content pack installed, every sample-based preset incorporates a secondary wavetable layer (10% to 30% level). Without the pack, presets play at -18 dB to -32 dBFS rather than muting entirely.

---

## 6. Installation Paths

On standalone Akai MPC hardware, factory content packs are installed alongside factory presets within the plugin bundle directory:

```text
Safian - VST - SynthyBoy/
├── Content/
│   ├── packs/
│   │   ├── com.safian.synthyboy.factory-wavetables/
│   │   │   ├── contentpack.json
│   │   │   └── *.wav (16 wavetable files)
│   │   └── com.safian.synthyboy.factory-samples/
│   │       ├── contentpack.json
│   │       ├── samples/ (*.wav)
│   │       ├── instruments/ (<name>/<name>.sfz + samples/)
│   │       └── impulses/ (*.wav)
│   └── presets/
│       ├── Bass/
│       ├── FX/
│       ├── Keys/
│       ├── Lead/
│       ├── Pad/
│       └── Pluck/
├── Presets/ (.xpl files for MPC Sounds browser)
├── Plugin Skins/
├── plugin.json
└── VST/
    ├── synthyboy-arm32.so (MPC Key 61, MPC One, MPC Live)
    └── synthyboy-arm64.so (MPC XL)
```
