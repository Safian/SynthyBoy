# Factory Preset Output Level Table (2026-11-20)

The owner's latest simple level rule (2026-11-20):
- Engine default: voice-count alpha 0.5, make-up gain 0 dB, limiter OFF.
- Target single-note peak at velocity 127: T = -4.5 dBFS across all categories.
- Full chords (4-note stock and ours, 6-note, 8-note at velocity 127) at most -0.5 dBFS without limiter.
- master.volume change d = min(T - note_peak, -0.5 - 0.05 - max_chord_peak).
- Velocity tracking = 100% for all presets (engine law lerp(1, v^2, t)).

| Category | Preset | Note Peak v127 (K61) | Note Peak v127 (XL) | 4-Note Chord Peak | 6-Note Chord Peak | 8-Note Chord Peak | v127 - v64 Diff | master.volume | Flag |
|---|---|---|---|---|---|---|---|---|---|
| Bass | 808 Boom | -5.51 | -5.51 | -0.82 | -1.14 | -0.59 | +11.91 | +0.7 | CHORD_LIMIT |
| Bass | Acid Saw Reso | -8.15 | -8.15 | -2.46 | -1.76 | -0.54 | +11.90 | -2.1 | CHORD_LIMIT |
| Bass | Bend Formant Growl | -5.22 | -4.55 | -2.39 | -1.89 | -2.42 | +6.22 | -8.6 | - |
| Bass | Click Attack Bass | -5.91 | -6.21 | -2.25 | -1.37 | -0.54 | +11.91 | -4.2 | CHORD_LIMIT |
| Bass | Comb Metal Bass | -6.45 | -6.45 | -0.88 | -0.72 | -0.59 | +11.91 | -0.7 | CHORD_LIMIT |
| Bass | Custom Shape Wobble | -7.13 | -7.13 | -1.49 | -1.64 | -0.59 | +11.91 | -3.0 | CHORD_LIMIT |
| Bass | Diode Saturated Bass | -11.32 | -11.32 | -5.78 | -4.57 | -4.82 | +11.91 | +6.0 | MAX_VOL |
| Bass | Dirty Distortion Bass | -4.50 | -4.50 | -4.50 | -4.50 | -4.50 | +0.14 | -4.5 | - |
| Bass | EMS Squelch Bass | -7.28 | -7.25 | -1.86 | -1.56 | -0.59 | +11.91 | -0.4 | CHORD_LIMIT |
| Bass | Fast Arp Bass | -8.88 | -8.88 | -3.19 | -1.76 | -0.52 | +11.91 | -8.6 | CHORD_LIMIT |
| Bass | Fat Reese | -7.54 | -7.21 | -1.83 | -0.95 | -0.54 | +11.91 | -6.7 | CHORD_LIMIT |
| Bass | Formant Vowel Bass | -6.85 | -6.85 | -2.73 | -2.22 | -0.56 | +11.90 | -7.6 | CHORD_LIMIT |
| Bass | Gritty Square Bass | -4.47 | -4.47 | -2.49 | -2.32 | -2.26 | +9.65 | -2.2 | - |
| Bass | Harmonic Sine Bass | -6.34 | -6.34 | -1.80 | -0.95 | -0.52 | +11.90 | -0.0 | CHORD_LIMIT |
| Bass | Ladder FM Bass | -4.62 | -4.62 | -0.58 | -1.71 | -2.22 | +10.34 | -4.4 | CHORD_LIMIT |
| Bass | Moog Ladder Bass | -8.08 | -8.08 | -2.52 | -1.45 | -0.58 | +11.90 | -1.0 | CHORD_LIMIT |
| Bass | Odd Harmonics Bass | -7.32 | -7.32 | -1.62 | -1.08 | -0.52 | +11.91 | -1.0 | CHORD_LIMIT |
| Bass | Pitch Drop 808 | -8.02 | -8.02 | -2.96 | -1.98 | -0.57 | +4.30 | +0.0 | CHORD_LIMIT |
| Bass | Pulse Width Bass | -8.53 | -8.53 | -2.52 | -0.91 | -0.52 | +11.91 | -1.4 | CHORD_LIMIT |
| Bass | Punchy Pluck Bass | -4.48 | -4.48 | -2.05 | -2.64 | -3.37 | +11.91 | +3.2 | - |
| Bass | Reese Dual Phase Bass | -5.40 | -4.54 | -3.78 | -4.09 | -3.81 | +10.83 | -16.2 | - |
| Bass | Scream Filter Bass | -8.69 | -8.69 | -3.06 | -1.70 | -0.54 | +11.90 | -8.0 | CHORD_LIMIT |
| Bass | Solid Sub | -5.08 | -5.08 | -1.85 | -0.51 | -0.71 | +11.90 | +1.7 | CHORD_LIMIT |
| Bass | Space Wobble Bass | -7.18 | -7.18 | -2.70 | -1.86 | -0.51 | +11.91 | -1.0 | CHORD_LIMIT |
| Bass | Sub Duo Octave | -7.70 | -7.70 | -1.93 | -0.94 | -0.55 | +11.91 | -7.2 | CHORD_LIMIT |
| Bass | Subterranean Duo | -4.54 | -4.54 | -2.94 | -3.58 | -3.37 | +11.38 | -7.5 | - |
| Bass | Sync Growl Bass | -4.51 | -4.51 | -2.01 | -2.23 | -2.48 | +11.91 | +4.7 | - |
| Bass | Triple Fold Reese | -4.53 | -4.53 | -4.61 | -3.66 | -3.98 | +5.06 | -8.9 | - |
| Bass | Warm Analog Bass | -8.48 | -8.48 | -2.59 | -1.59 | -0.54 | +11.90 | +3.5 | CHORD_LIMIT |
| FX | Alien Communication | -7.57 | -7.57 | -3.53 | -2.74 | -0.52 | +11.91 | -7.9 | CHORD_LIMIT |
| FX | Bode Barber Riser | -7.19 | -5.28 | -4.00 | -3.92 | -3.49 | +11.74 | +6.0 | MAX_VOL |
| FX | Bode Shifter Drone | -10.83 | -4.45 | -1.58 | -2.06 | -1.83 | +11.91 | -3.4 | - |
| FX | Breath of Machines | -6.16 | -4.87 | -2.38 | -0.54 | -0.79 | +12.28 | -7.1 | CHORD_LIMIT |
| FX | Cosmic Teleport | -7.76 | -7.76 | -1.75 | -1.63 | -0.59 | +11.91 | -0.6 | CHORD_LIMIT |
| FX | Detonation Tail | -4.51 | -4.98 | -4.99 | -5.18 | -5.12 | +1.12 | -3.4 | - |
| FX | Game Over Zap | -4.50 | -4.50 | -3.74 | -3.78 | -3.69 | +8.00 | -3.6 | - |
| FX | Gong Grain Swarm | -5.13 | -4.53 | -3.37 | -2.82 | -2.21 | +12.12 | +3.5 | - |
| FX | Metal Gong Strike | -8.03 | -5.23 | -1.83 | -1.02 | -0.60 | +11.90 | -7.5 | CHORD_LIMIT |
| FX | Noise Riser | -5.99 | -4.47 | -2.55 | -1.58 | -1.24 | +11.91 | -5.7 | - |
| FX | Reverse Gong Swell | -4.50 | -5.37 | -8.47 | -9.54 | -9.14 | +12.01 | +3.5 | - |
| FX | Rising Grain Riser | -7.00 | -7.35 | -6.63 | -7.20 | -7.45 | +12.27 | +6.0 | MAX_VOL |
| FX | Sci-Fi Laser Sweep | -7.54 | -7.54 | -2.37 | -1.42 | -0.54 | +11.91 | -1.9 | CHORD_LIMIT |
| FX | Sonar Ping | -8.25 | -6.64 | -2.02 | -1.84 | -0.56 | +11.91 | +4.4 | CHORD_LIMIT |
| FX | Space Wind Storm | -5.14 | -5.11 | -1.26 | -0.56 | -0.82 | +11.91 | -5.1 | CHORD_LIMIT |
| FX | Spectral Chord Morph | -4.45 | -4.48 | -2.16 | -2.51 | -0.75 | +11.93 | +1.3 | - |
| FX | Sub Drop Boom | -6.09 | -6.09 | -1.19 | -1.12 | -0.56 | +11.90 | +0.1 | CHORD_LIMIT |
| FX | Vinyl Dust Crackle | -5.03 | -4.86 | -1.08 | -1.86 | -0.59 | +11.91 | -4.6 | CHORD_LIMIT |
| Keys | Accordion Reed | -4.50 | -4.50 | -2.28 | -1.18 | -1.74 | +11.90 | -10.7 | - |
| Keys | B3 Drawbar Organ | -7.16 | -7.16 | -1.69 | -1.03 | -0.53 | +11.91 | -1.0 | CHORD_LIMIT |
| Keys | Breakbeat Slice Keys | -4.49 | -6.43 | -3.13 | -5.30 | -5.03 | +17.02 | -2.6 | - |
| Keys | Celesta Bells | -6.74 | -7.10 | -1.30 | -0.91 | -0.51 | +11.91 | -0.0 | CHORD_LIMIT |
| Keys | Clavinet Funk | -4.53 | -4.53 | -4.13 | -4.21 | -4.16 | +6.73 | -4.1 | - |
| Keys | Drawbar Organ Keys | -5.83 | -5.81 | -2.24 | -0.91 | -0.52 | +10.86 | -9.0 | CHORD_LIMIT |
| Keys | Dream Grand Piano | -5.60 | -5.96 | -0.99 | -1.08 | -0.58 | +11.91 | -4.4 | CHORD_LIMIT |
| Keys | Dusty Rhodes Keys | -4.93 | -4.55 | -2.27 | -2.67 | -2.25 | +8.61 | -7.9 | - |
| Keys | Dyno Electric Piano | -4.54 | -4.55 | -0.85 | -2.16 | -1.75 | +11.90 | +3.1 | - |
| Keys | FM Soft Organ | -6.58 | -6.58 | -1.50 | -1.09 | -0.53 | +11.91 | -0.4 | CHORD_LIMIT |
| Keys | Glass Organ Tones | -7.25 | -4.55 | -1.71 | -1.48 | -1.42 | +11.91 | -1.1 | - |
| Keys | Glass Zone Bells | -4.53 | -5.40 | -0.63 | -0.93 | -0.59 | +16.72 | -2.7 | - |
| Keys | Harpsichord Bar | -6.28 | -6.28 | -1.33 | -1.75 | -0.51 | +11.91 | +1.0 | CHORD_LIMIT |
| Keys | Just Intonation Keys | -5.21 | -5.49 | -1.64 | -1.08 | -0.59 | +11.90 | +0.4 | CHORD_LIMIT |
| Keys | Lo-Fi Rhodes | -4.55 | -4.54 | -1.10 | -1.21 | -2.74 | +11.91 | +1.5 | - |
| Keys | Marimba Layer Keys | -6.63 | -5.12 | -1.59 | -1.04 | -0.51 | +17.03 | -1.5 | CHORD_LIMIT |
| Keys | Muted Honky Tonk | -6.44 | -6.44 | -1.23 | -1.50 | -0.53 | +11.91 | -1.3 | CHORD_LIMIT |
| Keys | Rotary Rock Organ | -4.51 | -4.51 | -4.02 | -4.33 | -4.32 | +9.89 | -14.9 | - |
| Keys | Tempo Gate Chords | -4.45 | -4.55 | -2.53 | -2.62 | -2.08 | +12.17 | -3.7 | - |
| Keys | Tine Glass Keys | -6.68 | -7.16 | -2.17 | -1.66 | -0.52 | +12.50 | -5.6 | CHORD_LIMIT |
| Keys | Utility Width Keys | -5.15 | -5.15 | -0.51 | -1.89 | -1.09 | +11.90 | -4.2 | CHORD_LIMIT |
| Keys | Vibraphone Bars | -7.42 | -5.68 | -1.76 | -0.53 | -1.36 | +11.91 | -5.4 | CHORD_LIMIT |
| Keys | Vintage Electric Tine | -5.89 | -5.89 | -0.56 | -2.68 | -1.31 | +11.91 | -5.3 | CHORD_LIMIT |
| Keys | Wah Pressure Clav | -4.52 | -4.52 | -2.83 | -2.84 | -2.76 | +10.79 | +0.6 | - |
| Keys | Warm Toy Piano | -5.74 | -5.74 | -0.98 | -1.66 | -0.52 | +11.91 | +1.9 | CHORD_LIMIT |
| Lead | Bell Chime Lead | -6.13 | -6.13 | -1.92 | -1.59 | -0.50 | +11.90 | -2.3 | CHORD_LIMIT |
| Lead | Bell FM Ripple Lead | -6.83 | -7.15 | -1.93 | -1.41 | -0.57 | +12.35 | -5.1 | CHORD_LIMIT |
| Lead | Breath Flute Lead | -5.01 | -4.52 | -4.17 | -4.25 | -3.86 | +12.80 | -3.1 | - |
| Lead | Chorused Saw Solo | -5.04 | -4.54 | -2.12 | -2.11 | -1.28 | +11.91 | -8.1 | - |
| Lead | Cosmic Drift Lead | -9.95 | -4.46 | -4.59 | -4.49 | -4.25 | +11.91 | -6.1 | - |
| Lead | Diode Bite Lead | -11.18 | -11.18 | -5.16 | -3.41 | -2.18 | +11.90 | +6.0 | MAX_VOL |
| Lead | Dirty Peak Lead | -9.28 | -9.28 | -3.27 | -1.72 | -0.57 | +11.91 | -14.8 | CHORD_LIMIT |
| Lead | Dual Octave Hero | -9.84 | -9.84 | -2.92 | -1.58 | -0.52 | +11.91 | -7.6 | CHORD_LIMIT |
| Lead | FM Digital Lead | -6.98 | -6.98 | -1.34 | -0.58 | -0.58 | +11.90 | -0.8 | CHORD_LIMIT |
| Lead | Harmonic Shimmer Lead | -5.31 | -5.31 | -0.53 | -1.75 | -1.97 | +11.90 | -7.3 | CHORD_LIMIT |
| Lead | Hyper Saw Lead | -7.00 | -4.47 | -1.65 | -1.88 | -1.40 | +11.91 | -7.8 | - |
| Lead | Laser Beam Lead | -8.04 | -8.04 | -2.25 | -1.99 | -0.52 | +11.91 | -1.3 | CHORD_LIMIT |
| Lead | MPE Pressure Lead | -8.14 | -7.12 | -2.55 | -0.52 | -0.89 | +11.90 | -0.9 | CHORD_LIMIT |
| Lead | Metallic Edge Lead | -4.45 | -4.45 | -3.92 | -3.92 | -3.92 | +7.16 | -3.9 | - |
| Lead | Phaser Sweep Lead | -7.40 | -5.27 | -1.44 | -0.52 | -0.90 | +11.90 | -2.4 | CHORD_LIMIT |
| Lead | Plucked Lead Tone | -7.32 | -7.32 | -1.97 | -1.69 | -0.56 | +11.90 | +0.6 | CHORD_LIMIT |
| Lead | Pulse Width Mod Lead | -4.46 | -4.75 | -2.47 | -3.38 | -3.06 | +11.90 | -4.7 | - |
| Lead | Screaming Sync Lead | -4.55 | -4.55 | -3.43 | -3.23 | -3.14 | +9.19 | -2.8 | - |
| Lead | Solo Square Lead | -4.50 | -4.50 | -4.50 | -4.50 | -4.49 | +11.91 | +2.8 | - |
| Lead | Spectral Bowed Glass | -5.21 | -4.47 | -3.70 | -4.14 | -4.36 | +11.75 | -2.6 | - |
| Lead | Staccato Bleep Lead | -4.51 | -4.51 | -1.44 | -3.31 | -3.04 | +11.90 | +3.2 | - |
| Lead | Sync Cry Lead | -5.97 | -5.45 | -0.99 | -1.65 | -0.56 | +12.51 | -1.6 | CHORD_LIMIT |
| Lead | Tri Soft Solo | -6.69 | -5.18 | -1.83 | -0.51 | -0.91 | +11.91 | -0.3 | CHORD_LIMIT |
| Lead | Triple Fifth Lead | -4.46 | -4.69 | -2.83 | -3.41 | -3.27 | +12.76 | -5.1 | - |
| Lead | Twin Warp Lead | -9.34 | -9.35 | -2.75 | -1.44 | -0.51 | +11.90 | -2.3 | CHORD_LIMIT |
| Lead | Vocaloid Lead | -7.53 | -7.53 | -3.48 | -2.86 | -0.52 | +11.90 | -9.8 | CHORD_LIMIT |
| Lead | Vowel Scrub Choir | -4.49 | -4.91 | -2.52 | -3.13 | -2.96 | +12.04 | -1.2 | - |
| Lead | Vowel Table Lead | -6.41 | -4.51 | -2.47 | -2.53 | -5.05 | +11.59 | -4.3 | - |
| Lead | Vowel Whistle Lead | -5.01 | -4.50 | -1.63 | -1.86 | -3.95 | +10.92 | -4.4 | - |
| Lead | Warm Vintage Lead | -4.55 | -4.55 | -1.91 | -1.77 | -1.80 | +11.90 | -2.0 | - |
| Pad | Bowed MPE Strings | -4.50 | -4.64 | -1.15 | -1.31 | -1.73 | +12.11 | -1.3 | - |
| Pad | Bowed Pressure Strings | -5.15 | -5.09 | -2.27 | -1.31 | -0.59 | +12.62 | -0.5 | CHORD_LIMIT |
| Pad | Brass Swell Ensemble | -4.49 | -5.12 | -1.98 | -2.84 | -2.81 | +13.08 | -4.7 | - |
| Pad | Cathedral Choir | -6.67 | -5.18 | -2.02 | -1.19 | -0.56 | +11.90 | +0.0 | CHORD_LIMIT |
| Pad | Chord Cloud Pad | -4.49 | -4.58 | -4.35 | -6.10 | -4.28 | +11.96 | +4.0 | - |
| Pad | Comb Reso Swell | -5.95 | -5.95 | -1.53 | -0.51 | -0.69 | +11.91 | -2.9 | CHORD_LIMIT |
| Pad | Dark Cinema Drone | -6.52 | -4.51 | -2.23 | -2.08 | -1.82 | +11.90 | -4.6 | - |
| Pad | Deep Sea Ambient | -6.76 | -5.16 | -1.73 | -1.67 | -0.56 | +11.90 | +3.1 | CHORD_LIMIT |
| Pad | Diffusor Cloud Pad | -5.97 | -4.50 | -1.60 | -2.14 | -1.05 | +11.90 | -4.1 | - |
| Pad | Ethereal Glass | -6.35 | -4.51 | -2.50 | -3.25 | -2.36 | +11.91 | -2.5 | - |
| Pad | Frozen Aurora | -9.00 | -4.51 | -3.00 | -3.03 | -2.23 | +11.90 | -4.2 | - |
| Pad | Frozen Pluck Drone | -4.48 | -6.01 | -3.83 | -5.33 | -4.05 | +12.19 | +1.4 | - |
| Pad | Gentle Flute Pad | -4.78 | -4.53 | -1.59 | -1.57 | -1.45 | +11.91 | +0.0 | - |
| Pad | Golden Dawn Pad | -7.37 | -5.33 | -1.93 | -1.77 | -0.59 | +11.90 | -6.4 | CHORD_LIMIT |
| Pad | Harmonic Cloud | -7.60 | -4.52 | -2.18 | -3.57 | -3.41 | +11.91 | -4.2 | - |
| Pad | Hyper Dimension Pad | -8.29 | -5.38 | -0.56 | -0.67 | -0.55 | +11.90 | -0.4 | CHORD_LIMIT |
| Pad | Infinite Horizon | -8.58 | -4.62 | -0.65 | -1.68 | -0.56 | +11.91 | -3.6 | CHORD_LIMIT |
| Pad | Lorenz Garden | -5.93 | -5.93 | -1.52 | -1.62 | -0.55 | +13.20 | -6.9 | CHORD_LIMIT |
| Pad | Lush Analog Pad | -8.43 | -4.48 | -3.07 | -3.11 | -2.06 | +11.91 | -11.6 | - |
| Pad | Mid Side Halo | -4.50 | -4.65 | -1.83 | -2.00 | -1.58 | +9.46 | -2.6 | - |
| Pad | Odd Harmonics Pad | -4.53 | -4.53 | -2.20 | -3.77 | -2.87 | +11.90 | -8.4 | - |
| Pad | Parallel Dusk Pad | -5.70 | -4.54 | -2.52 | -1.84 | -2.09 | +12.91 | -12.3 | - |
| Pad | Radiant Pulse Pad | -4.50 | -4.50 | -3.68 | -3.98 | -3.24 | +11.90 | -7.2 | - |
| Pad | Shepard Ascent | -4.47 | -4.93 | -1.20 | -3.47 | -2.42 | +12.52 | +5.4 | - |
| Pad | Slow Motion Waves | -7.25 | -7.25 | -2.07 | -1.50 | -0.55 | +11.91 | +0.2 | CHORD_LIMIT |
| Pad | Space Odyssey Pad | -8.83 | -4.53 | -2.97 | -3.02 | -1.97 | +11.91 | -4.2 | - |
| Pad | Spectral Gong Pad | -5.70 | -5.16 | -4.06 | -3.95 | -3.23 | +12.14 | +6.0 | MAX_VOL |
| Pad | Spectral Vowel Freeze | -6.19 | -4.46 | -1.25 | -1.50 | -1.98 | +11.92 | -2.9 | - |
| Pad | Starlight Shimmer | -8.01 | -5.93 | -2.44 | -1.43 | -0.53 | +11.90 | -1.8 | CHORD_LIMIT |
| Pad | Stretched Glass Choir | -5.30 | -6.30 | -2.18 | -1.12 | -0.55 | +12.16 | -7.4 | CHORD_LIMIT |
| Pad | Subtle Tape Drift | -4.49 | -4.49 | -2.63 | -2.02 | -1.27 | +11.90 | -6.5 | - |
| Pad | Throat Singer Drone | -5.77 | -4.47 | -4.16 | -4.08 | -2.10 | +12.18 | -3.4 | - |
| Pad | Tri-Band Split Texture | -4.63 | -4.49 | -2.36 | -1.45 | -1.33 | +4.89 | -7.6 | - |
| Pad | Velvet Strings | -4.48 | -7.31 | -2.99 | -2.99 | -2.07 | +11.91 | -8.3 | - |
| Pad | Vintage Brass Pad | -4.54 | -4.71 | -2.40 | -2.44 | -1.65 | +11.91 | -8.3 | - |
| Pad | Vowel Choir Pad | -7.23 | -6.20 | -2.66 | -2.15 | -0.51 | +11.91 | -7.9 | CHORD_LIMIT |
| Pluck | Acoustic Style Pluck | -4.81 | -4.81 | -0.62 | -0.76 | -0.58 | +11.91 | +4.1 | CHORD_LIMIT |
| Pluck | Aux Keyscale Pluck | -4.97 | -5.01 | -0.51 | -0.94 | -0.62 | +11.91 | +4.2 | CHORD_LIMIT |
| Pluck | Bell Mallet Pluck | -4.96 | -4.96 | -0.55 | -1.38 | -1.44 | +11.91 | +3.1 | CHORD_LIMIT |
| Pluck | Chime Drop | -5.31 | -5.67 | -0.53 | -1.13 | -0.74 | +11.91 | +1.4 | CHORD_LIMIT |
| Pluck | Comb Zither | -4.68 | -4.68 | -0.56 | -2.30 | -1.82 | +11.91 | -1.8 | CHORD_LIMIT |
| Pluck | Digital Glass Tap | -4.54 | -4.54 | -3.61 | -5.84 | -7.09 | +11.91 | +2.0 | - |
| Pluck | Electric Mute Pluck | -4.47 | -4.47 | -3.53 | -3.63 | -3.65 | +8.87 | -3.0 | - |
| Pluck | Formant Pop Pluck | -5.12 | -5.12 | -1.89 | -1.79 | -0.56 | +11.90 | -2.9 | CHORD_LIMIT |
| Pluck | Glass Ping Pluck | -6.10 | -7.03 | -2.46 | -1.59 | -0.58 | +13.06 | -5.2 | CHORD_LIMIT |
| Pluck | Harmonic Tine Pluck | -4.51 | -4.51 | -0.91 | -3.13 | -2.60 | +11.91 | +1.8 | - |
| Pluck | Harp Drops | -4.51 | -4.87 | -1.54 | -3.19 | -2.72 | +11.90 | +3.2 | - |
| Pluck | Kalimba Tines | -4.52 | -4.52 | -2.51 | -1.70 | -0.89 | +11.91 | -1.8 | - |
| Pluck | Karplus Harp | -4.50 | -4.50 | -4.50 | -4.50 | -4.50 | +2.72 | +4.5 | - |
| Pluck | Marimba Synth | -4.50 | -4.50 | -2.88 | -4.72 | -5.10 | +11.91 | +1.9 | - |
| Pluck | Nylon Zone Pluck | -6.75 | -6.94 | -2.17 | -2.02 | -0.59 | +16.99 | +0.9 | CHORD_LIMIT |
| Pluck | Pentatonic Step Pluck | -4.50 | -4.50 | -4.50 | -4.50 | -4.50 | +9.44 | +3.5 | - |
| Pluck | Pizzicato Strings | -4.54 | -4.58 | -0.84 | -1.27 | -1.38 | +11.91 | +1.1 | - |
| Pluck | Ring Mod Gamelan | -4.55 | -4.71 | -1.80 | -2.32 | -0.63 | +11.90 | +3.1 | - |
| Pluck | Rubber Bass Pluck | -4.47 | -4.47 | -0.56 | -2.60 | -2.62 | +11.90 | +1.3 | - |
| Pluck | Spruce Guitar | -5.21 | -5.21 | -0.63 | -0.75 | -0.58 | +11.90 | +2.9 | CHORD_LIMIT |
| Pluck | Stutter Grain Kit | -4.54 | -4.54 | -5.23 | -6.19 | -5.58 | +15.47 | +0.3 | - |
| Pluck | Trance Short Pluck | -4.54 | -4.54 | -1.23 | -1.43 | -1.30 | +11.90 | +5.2 | - |

## Summary Statistics (all 160 presets)

| Metric | Median | Min | Max | Target / Window |
|---|---|---|---|---|
| Note Peak v127 (Key 61) | -5.70 dBFS | -11.32 dBFS | -4.45 dBFS | -4.50 dBFS |
| Note Peak v127 (XL) | -5.09 dBFS | -11.32 dBFS | -4.45 dBFS | -4.50 dBFS |
| 4-Note Chord Peak | -2.18 dBFS | -8.47 dBFS | -0.51 dBFS | <= -0.50 dBFS |
| 6-Note Chord Peak | -1.85 dBFS | -9.54 dBFS | -0.51 dBFS | <= -0.50 dBFS |
| 8-Note Chord Peak | -1.29 dBFS | -9.14 dBFS | -0.50 dBFS | <= -0.50 dBFS |
| v127 - v64 Level Diff | +11.91 dB | +0.14 dB | +17.03 dB | ~12 dB (window 10..14 dB) |
| master.volume | -2.3 dB | -16.2 dB | +6.0 dB | [-60 .. +6] dB |

### Chord Peak Verification
- Chords over -0.5 dBFS: 0
- PASS: Every 4/6/8-note chord across both profiles stays <= -0.5 dBFS.

### Velocity Dynamics Verification (v127 vs v64)
- Median difference: +11.91 dB (expected ~12 dB with velocity_track 100 %)
- Presets outside 10..14 dB window: 22
  - Bend Formant Growl: +6.22 dB
  - Dirty Distortion Bass: +0.14 dB
  - Gritty Square Bass: +9.65 dB
  - Pitch Drop 808: +4.30 dB
  - Triple Fold Reese: +5.06 dB
  - Detonation Tail: +1.12 dB
  - Game Over Zap: +8.00 dB
  - Breakbeat Slice Keys: +17.02 dB
  - Clavinet Funk: +6.73 dB
  - Dusty Rhodes Keys: +8.61 dB
  - Glass Zone Bells: +16.72 dB
  - Marimba Layer Keys: +17.03 dB
  - Rotary Rock Organ: +9.89 dB
  - Metallic Edge Lead: +7.16 dB
  - Screaming Sync Lead: +9.19 dB
  - Mid Side Halo: +9.46 dB
  - Tri-Band Split Texture: +4.89 dB
  - Electric Mute Pluck: +8.87 dB
  - Karplus Harp: +2.72 dB
  - Nylon Zone Pluck: +16.99 dB
  - Pentatonic Step Pluck: +9.44 dB
  - Stutter Grain Kit: +15.47 dB

### Presets Missing Target T = -4.5 dBFS (83 presets)
- 808 Boom (Bass): reached -5.51 dBFS (missing 1.01 dB; reason: chord peak limit (-0.55 dBFS))
- Acid Saw Reso (Bass): reached -8.15 dBFS (missing 3.65 dB; reason: chord peak limit (-0.55 dBFS))
- Click Attack Bass (Bass): reached -5.91 dBFS (missing 1.41 dB; reason: chord peak limit (-0.55 dBFS))
- Comb Metal Bass (Bass): reached -6.45 dBFS (missing 1.95 dB; reason: chord peak limit (-0.55 dBFS))
- Custom Shape Wobble (Bass): reached -7.13 dBFS (missing 2.63 dB; reason: chord peak limit (-0.55 dBFS))
- Diode Saturated Bass (Bass): reached -11.32 dBFS (missing 6.82 dB; reason: hit master.volume max (+6 dB))
- EMS Squelch Bass (Bass): reached -7.25 dBFS (missing 2.75 dB; reason: chord peak limit (-0.55 dBFS))
- Fast Arp Bass (Bass): reached -8.88 dBFS (missing 4.38 dB; reason: chord peak limit (-0.55 dBFS))
- Fat Reese (Bass): reached -7.21 dBFS (missing 2.71 dB; reason: chord peak limit (-0.55 dBFS))
- Formant Vowel Bass (Bass): reached -6.85 dBFS (missing 2.35 dB; reason: chord peak limit (-0.55 dBFS))
- Harmonic Sine Bass (Bass): reached -6.34 dBFS (missing 1.84 dB; reason: chord peak limit (-0.55 dBFS))
- Ladder FM Bass (Bass): reached -4.62 dBFS (missing 0.12 dB; reason: chord peak limit (-0.55 dBFS))
- Moog Ladder Bass (Bass): reached -8.08 dBFS (missing 3.58 dB; reason: chord peak limit (-0.55 dBFS))
- Odd Harmonics Bass (Bass): reached -7.32 dBFS (missing 2.82 dB; reason: chord peak limit (-0.55 dBFS))
- Pitch Drop 808 (Bass): reached -8.02 dBFS (missing 3.52 dB; reason: chord peak limit (-0.55 dBFS))
- Pulse Width Bass (Bass): reached -8.53 dBFS (missing 4.03 dB; reason: chord peak limit (-0.55 dBFS))
- Scream Filter Bass (Bass): reached -8.69 dBFS (missing 4.19 dB; reason: chord peak limit (-0.55 dBFS))
- Solid Sub (Bass): reached -5.08 dBFS (missing 0.58 dB; reason: chord peak limit (-0.55 dBFS))
- Space Wobble Bass (Bass): reached -7.18 dBFS (missing 2.68 dB; reason: chord peak limit (-0.55 dBFS))
- Sub Duo Octave (Bass): reached -7.70 dBFS (missing 3.20 dB; reason: chord peak limit (-0.55 dBFS))
- Warm Analog Bass (Bass): reached -8.48 dBFS (missing 3.98 dB; reason: chord peak limit (-0.55 dBFS))
- Alien Communication (FX): reached -7.57 dBFS (missing 3.07 dB; reason: chord peak limit (-0.55 dBFS))
- Bode Barber Riser (FX): reached -5.28 dBFS (missing 0.78 dB; reason: hit master.volume max (+6 dB))
- Breath of Machines (FX): reached -4.87 dBFS (missing 0.37 dB; reason: chord peak limit (-0.55 dBFS))
- Cosmic Teleport (FX): reached -7.76 dBFS (missing 3.26 dB; reason: chord peak limit (-0.55 dBFS))
- Metal Gong Strike (FX): reached -5.23 dBFS (missing 0.73 dB; reason: chord peak limit (-0.55 dBFS))
- Rising Grain Riser (FX): reached -7.00 dBFS (missing 2.50 dB; reason: hit master.volume max (+6 dB))
- Sci-Fi Laser Sweep (FX): reached -7.54 dBFS (missing 3.04 dB; reason: chord peak limit (-0.55 dBFS))
- Sonar Ping (FX): reached -6.64 dBFS (missing 2.14 dB; reason: chord peak limit (-0.55 dBFS))
- Space Wind Storm (FX): reached -5.11 dBFS (missing 0.61 dB; reason: chord peak limit (-0.55 dBFS))
- Sub Drop Boom (FX): reached -6.09 dBFS (missing 1.59 dB; reason: chord peak limit (-0.55 dBFS))
- Vinyl Dust Crackle (FX): reached -4.86 dBFS (missing 0.36 dB; reason: chord peak limit (-0.55 dBFS))
- B3 Drawbar Organ (Keys): reached -7.16 dBFS (missing 2.66 dB; reason: chord peak limit (-0.55 dBFS))
- Celesta Bells (Keys): reached -6.74 dBFS (missing 2.24 dB; reason: chord peak limit (-0.55 dBFS))
- Drawbar Organ Keys (Keys): reached -5.81 dBFS (missing 1.31 dB; reason: chord peak limit (-0.55 dBFS))
- Dream Grand Piano (Keys): reached -5.60 dBFS (missing 1.10 dB; reason: chord peak limit (-0.55 dBFS))
- FM Soft Organ (Keys): reached -6.58 dBFS (missing 2.08 dB; reason: chord peak limit (-0.55 dBFS))
- Harpsichord Bar (Keys): reached -6.28 dBFS (missing 1.78 dB; reason: chord peak limit (-0.55 dBFS))
- Just Intonation Keys (Keys): reached -5.21 dBFS (missing 0.71 dB; reason: chord peak limit (-0.55 dBFS))
- Marimba Layer Keys (Keys): reached -5.12 dBFS (missing 0.62 dB; reason: chord peak limit (-0.55 dBFS))
- Muted Honky Tonk (Keys): reached -6.44 dBFS (missing 1.94 dB; reason: chord peak limit (-0.55 dBFS))
- Tine Glass Keys (Keys): reached -6.68 dBFS (missing 2.18 dB; reason: chord peak limit (-0.55 dBFS))
- Utility Width Keys (Keys): reached -5.15 dBFS (missing 0.65 dB; reason: chord peak limit (-0.55 dBFS))
- Vibraphone Bars (Keys): reached -5.68 dBFS (missing 1.18 dB; reason: chord peak limit (-0.55 dBFS))
- Vintage Electric Tine (Keys): reached -5.89 dBFS (missing 1.39 dB; reason: chord peak limit (-0.55 dBFS))
- Warm Toy Piano (Keys): reached -5.74 dBFS (missing 1.24 dB; reason: chord peak limit (-0.55 dBFS))
- Bell Chime Lead (Lead): reached -6.13 dBFS (missing 1.63 dB; reason: chord peak limit (-0.55 dBFS))
- Bell FM Ripple Lead (Lead): reached -6.83 dBFS (missing 2.33 dB; reason: chord peak limit (-0.55 dBFS))
- Diode Bite Lead (Lead): reached -11.18 dBFS (missing 6.68 dB; reason: hit master.volume max (+6 dB))
- Dirty Peak Lead (Lead): reached -9.28 dBFS (missing 4.78 dB; reason: chord peak limit (-0.55 dBFS))
- Dual Octave Hero (Lead): reached -9.84 dBFS (missing 5.34 dB; reason: chord peak limit (-0.55 dBFS))
- FM Digital Lead (Lead): reached -6.98 dBFS (missing 2.48 dB; reason: chord peak limit (-0.55 dBFS))
- Harmonic Shimmer Lead (Lead): reached -5.31 dBFS (missing 0.81 dB; reason: chord peak limit (-0.55 dBFS))
- Laser Beam Lead (Lead): reached -8.04 dBFS (missing 3.54 dB; reason: chord peak limit (-0.55 dBFS))
- MPE Pressure Lead (Lead): reached -7.12 dBFS (missing 2.62 dB; reason: chord peak limit (-0.55 dBFS))
- Phaser Sweep Lead (Lead): reached -5.27 dBFS (missing 0.77 dB; reason: chord peak limit (-0.55 dBFS))
- Plucked Lead Tone (Lead): reached -7.32 dBFS (missing 2.82 dB; reason: chord peak limit (-0.55 dBFS))
- Sync Cry Lead (Lead): reached -5.45 dBFS (missing 0.95 dB; reason: chord peak limit (-0.55 dBFS))
- Tri Soft Solo (Lead): reached -5.18 dBFS (missing 0.68 dB; reason: chord peak limit (-0.55 dBFS))
- Twin Warp Lead (Lead): reached -9.34 dBFS (missing 4.84 dB; reason: chord peak limit (-0.55 dBFS))
- Vocaloid Lead (Lead): reached -7.53 dBFS (missing 3.03 dB; reason: chord peak limit (-0.55 dBFS))
- Bowed Pressure Strings (Pad): reached -5.09 dBFS (missing 0.59 dB; reason: chord peak limit (-0.55 dBFS))
- Cathedral Choir (Pad): reached -5.18 dBFS (missing 0.68 dB; reason: chord peak limit (-0.55 dBFS))
- Comb Reso Swell (Pad): reached -5.95 dBFS (missing 1.45 dB; reason: chord peak limit (-0.55 dBFS))
- Deep Sea Ambient (Pad): reached -5.16 dBFS (missing 0.66 dB; reason: chord peak limit (-0.55 dBFS))
- Golden Dawn Pad (Pad): reached -5.33 dBFS (missing 0.83 dB; reason: chord peak limit (-0.55 dBFS))
- Hyper Dimension Pad (Pad): reached -5.38 dBFS (missing 0.88 dB; reason: chord peak limit (-0.55 dBFS))
- Infinite Horizon (Pad): reached -4.62 dBFS (missing 0.12 dB; reason: chord peak limit (-0.55 dBFS))
- Lorenz Garden (Pad): reached -5.93 dBFS (missing 1.43 dB; reason: chord peak limit (-0.55 dBFS))
- Slow Motion Waves (Pad): reached -7.25 dBFS (missing 2.75 dB; reason: chord peak limit (-0.55 dBFS))
- Spectral Gong Pad (Pad): reached -5.16 dBFS (missing 0.66 dB; reason: hit master.volume max (+6 dB))
- Starlight Shimmer (Pad): reached -5.93 dBFS (missing 1.43 dB; reason: chord peak limit (-0.55 dBFS))
- Stretched Glass Choir (Pad): reached -5.30 dBFS (missing 0.80 dB; reason: chord peak limit (-0.55 dBFS))
- Vowel Choir Pad (Pad): reached -6.20 dBFS (missing 1.70 dB; reason: chord peak limit (-0.55 dBFS))
- Acoustic Style Pluck (Pluck): reached -4.81 dBFS (missing 0.31 dB; reason: chord peak limit (-0.55 dBFS))
- Aux Keyscale Pluck (Pluck): reached -4.97 dBFS (missing 0.47 dB; reason: chord peak limit (-0.55 dBFS))
- Bell Mallet Pluck (Pluck): reached -4.96 dBFS (missing 0.46 dB; reason: chord peak limit (-0.55 dBFS))
- Chime Drop (Pluck): reached -5.31 dBFS (missing 0.81 dB; reason: chord peak limit (-0.55 dBFS))
- Comb Zither (Pluck): reached -4.68 dBFS (missing 0.18 dB; reason: chord peak limit (-0.55 dBFS))
- Formant Pop Pluck (Pluck): reached -5.12 dBFS (missing 0.62 dB; reason: chord peak limit (-0.55 dBFS))
- Glass Ping Pluck (Pluck): reached -6.10 dBFS (missing 1.60 dB; reason: chord peak limit (-0.55 dBFS))
- Nylon Zone Pluck (Pluck): reached -6.75 dBFS (missing 2.25 dB; reason: chord peak limit (-0.55 dBFS))
- Spruce Guitar (Pluck): reached -5.21 dBFS (missing 0.71 dB; reason: chord peak limit (-0.55 dBFS))

