| Name | Group | Folder | Oscillators | Effects | Loudness (LU) | CPU (p99) | Status |


# Proposed Reduced Set (30 presets)

| Name | Group | Type |
|---|---|---|
| Fat Reese | Bass | WAVE |
| Custom Shape Wobble | Bass | WAVE |
| Diode Saturated Bass | Bass | MULTI |
| Odd Harmonics Bass | Bass | WAVE |
| Space Wobble Bass | Bass | MULTI |
| Warm Analog Bass | Bass | WAVE |
| Formant Vowel Bass | Bass | SPEC |
| Click Attack Bass | Bass | MULTI |
| Pulse Width Bass | Bass | WAVE |
| Space Wind Storm | FX | SAMPLE |
| Vinyl Dust Crackle | FX | SAMPLE |
| Detonation Tail | FX | SAMPLE |
| Bode Shifter Drone | FX | SPEC |
| Bowed MPE Strings | Pad | MULTI |
| Bowed Pressure Strings | Pad | MULTI |
| Cathedral Choir | Pad | SPEC |
| Dark Cinema Drone | Pad | SPEC |
| Starlight Shimmer | Pad | SPEC |
| Lush Analog Pad | Pad | WAVE |
| Harmonic Shimmer Lead | Lead | WAVE |
| Pulse Width Mod Lead | Lead | WAVE |
| Solo Square Lead | Lead | WAVE |
| Muted Honky Tonk | Keys | MULTI |
| Dream Grand Piano | Keys | MULTI |
| Drawbar Organ Keys | Keys | MULTI |
| Celesta Bells | Keys | MULTI |
| Spruce Guitar | Pluck | MULTI |
| Acoustic Style Pluck | Pluck | MULTI |
| Rubber Bass Pluck | Pluck | WAVE |
| Aux Keyscale Pluck | Pluck | WAVE |



# Effect on Preset Counting Files

The follow scripts rely on the hardcoded factory set count of 160:
- `scripts/gen-factory-presets.py`: This script generates the factory presets. It needs a mechanism to load a subset of presets based on a list file (e.g., `scripts/release/reduced-set-list.txt`).
- `scripts/gen-xpl-presets.py`: Used in tests for xpl generation.

Mechanism to ship a subset:
Introduce a `scripts/release/reduced-set-list.txt` file containing the names of the 30 included presets. The generation scripts (`gen-factory-presets.py`, `gen-xpl-presets.py`) should be modified to check for this list file and, if present, generate only the presets listed.

| Bode Barber Riser | FX | FX | 1, 6 | None | -10.6 | N/A | TODO |
| Breath of Machines | FX | FX | 1 | None | -21.5 | 66.5 -> 53.4 | TODO |
| Sci-Fi Laser Sweep | FX | FX | 1 | None | -12.4 | N/A | TODO |
| Gong Grain Swarm | FX | FX | 1, 12 | None | -5.8 | N/A | TODO |
| Sub Drop Boom | FX | FX | 0 | None | -13.2 | N/A | TODO |
| Alien Communication | FX | FX | 9 | None | -10.1 | N/A | TODO |
| Cosmic Teleport | FX | FX | 6 | None | -14.0 | N/A | TODO |
| Game Over Zap | FX | FX | 2 | None | -20.9 | N/A | TODO |
| Metal Gong Strike | FX | FX | 12 | None | -18.6 | N/A | TODO |
| Noise Riser | FX | FX |  | None | -19.6 | N/A | TODO |
| Space Wind Storm | FX | FX | 1 | None | -20.1 | N/A | PASSED |
| Spectral Chord Morph | FX | FX | 1, 6 | None | -10.8 | 22.6 -> 26.8 | TODO |
| Vinyl Dust Crackle | FX | FX | 1 | None | -16.5 | N/A | PASSED |
| Reverse Gong Swell | FX | FX | 1 | Convolve | -5.4 | N/A | TODO |
| Rising Grain Riser | FX | FX | 1, 1 | None | -6.1 | N/A | TODO |
| Sonar Ping | FX | FX | 0 | None | -7.1 | N/A | TODO |
| Detonation Tail | FX | FX | 0, 1 | None | -18.3 | N/A | PASSED |
| Bode Shifter Drone | FX | FX | 1 | None | -10.0 | N/A | PASSED |
| Pizzicato Strings | Pluck | Pluck | 1 | None | -0.5 | N/A | TODO |
| Bell Mallet Pluck | Pluck | Pluck | 12 | None | -2.7 | N/A | TODO |
| Harmonic Tine Pluck | Pluck | Pluck | 7 | None | -4.9 | N/A | TODO |
| Trance Short Pluck | Pluck | Pluck | 1 | None | 0.7 | N/A | TODO |
| Glass Ping Pluck | Pluck | Pluck | 1, 12 | Convolve | -7.9 | N/A | TODO |
| Karplus Harp | Pluck | Pluck |  | None | -8.4 | 67.0 -> 53.5 | TODO |
| Kalimba Tines | Pluck | Pluck | 0, 12 | None | -6.8 | N/A | TODO |
| Chime Drop | Pluck | Pluck | 12 | None | -4.1 | N/A | TODO |
| Spruce Guitar | Pluck | Pluck | 5 | None | -2.0 | N/A | TODO |
| Stutter Grain Kit | Pluck | Pluck | 1, 0 | None | -1.8 | N/A | TODO |
| Ring Mod Gamelan | Pluck | Pluck | 3, 12 | None | -6.7 | 84.6 -> 42.2 | TODO |
| Pentatonic Step Pluck | Pluck | Pluck | 1, 4 | None | -0.6 | 73.4 -> 60.5 | TODO |
| Marimba Synth | Pluck | Pluck | 3 | None | -2.6 | N/A | TODO |
| Comb Zither | Pluck | Pluck | 1 | None | -1.1 | 19.7 -> 18.0 (-1.7) | TODO |
| Nylon Zone Pluck | Pluck | Pluck | 1, 1 | None | -2.0 | N/A | TODO |
| Harp Drops | Pluck | Pluck | 3 | None | -4.7 | N/A | TODO |
| Electric Mute Pluck | Pluck | Pluck | 4 | None | -7.8 | N/A | TODO |
| Digital Glass Tap | Pluck | Pluck | 10 | None | -1.0 | N/A | TODO |
| Rubber Bass Pluck | Pluck | Pluck | 2 | None | -5.2 | N/A | TODO |
| Aux Keyscale Pluck | Pluck | Pluck | 1 | None | -5.0 | N/A | TODO |
| Acoustic Style Pluck | Pluck | Pluck | 1 | None | -0.9 | N/A | TODO |
| Formant Pop Pluck | Pluck | Pluck | 9 | None | -0.7 | N/A | TODO |
| Scream Filter Bass | Bass | Bass | 1 | None | -20.9 | N/A | TODO |
| 808 Boom | Bass | Bass | 0 | None | -13.6 | N/A | TODO |
| Dirty Distortion Bass | Bass | Bass | 4 | None | -27.0 | N/A | TODO |
| Sync Growl Bass | Bass | Bass | 1 | None | -8.8 | N/A | TODO |
| Gritty Square Bass | Bass | Bass | 2 | None | -24.4 | N/A | TODO |
| Triple Fold Reese | Bass | Bass | 1, 1, 5 | None | -23.6 | 75.5 -> 47.9 | TODO |
| Custom Shape Wobble | Bass | Bass | 1 | None | -13.4 | N/A | PASSED |
| Bend Formant Growl | Bass | Bass | 1 | None | -24.5 | 59.2 -> 42.6 | TODO |
| Comb Metal Bass | Bass | Bass | 1 | None | -13.7 | N/A | TODO |
| Moog Ladder Bass | Bass | Bass | 1 | None | -11.8 | N/A | TODO |
| Fast Arp Bass | Bass | Bass | 1 | None | -16.6 | N/A | TODO |
| Fat Reese | Bass | Bass | 1, 1 | None | -19.3 | N/A | PASSED |
| Harmonic Sine Bass | Bass | Bass | 7 | None | -16.1 | N/A | TODO |
| Solid Sub | Bass | Bass | 0 | None | -15.2 | N/A | TODO |
| Pitch Drop 808 | Bass | Bass | 0, 4 | None | -16.2 | N/A | TODO |
| Diode Saturated Bass | Bass | Bass | 5 | None | -5.7 | N/A | PASSED |
| Ladder FM Bass | Bass | Bass | 13 | None | -19.2 | 15.7 -> 14.7 (-0.9) | TODO |
| Odd Harmonics Bass | Bass | Bass | 8 | None | -16.4 | N/A | PASSED |
| Space Wobble Bass | Bass | Bass | 1 | None | -14.0 | N/A | PASSED |
| Warm Analog Bass | Bass | Bass | 5 | None | -6.2 | N/A | PASSED |
| Sub Duo Octave | Bass | Bass | 0, 0 | None | -20.0 | N/A | TODO |
| Subterranean Duo | Bass | Bass | 1 | None | -21.1 | 59.5 -> 36.9 | TODO |
| Punchy Pluck Bass | Bass | Bass | 2 | None | -8.0 | N/A | TODO |
| EMS Squelch Bass | Bass | Bass | 2 | None | -10.2 | N/A | TODO |
| Formant Vowel Bass | Bass | Bass | 9 | None | -20.3 | N/A | PASSED |
| Reese Dual Phase Bass | Bass | Bass | 13, 1 | Convolve | -31.5 | N/A | TODO |
| Click Attack Bass | Bass | Bass | 2 | None | -19.1 | N/A | PASSED |
| Pulse Width Bass | Bass | Bass | 11 | None | -16.4 | N/A | PASSED |
| Acid Saw Reso | Bass | Bass | 1 | None | -12.6 | N/A | TODO |
| Stretched Glass Choir | Pad | Pad | 6, 7 | None | -19.1 | 65.8 -> 51.0 | TODO |
| Gentle Flute Pad | Pad | Pad | 3 | None | -13.8 | N/A | TODO |
| Cathedral Choir | Pad | Pad | 9 | None | -10.1 | N/A | TODO |
| Mid Side Halo | Pad | Pad | 1, 3, 5 | None | -14.6 | 77.0 -> 63.4 | TODO |
| Dark Cinema Drone | Pad | Pad | 1, 2 | None | -17.9 | N/A | TODO |
| Starlight Shimmer | Pad | Pad | 12 | None | -14.2 | N/A | TODO |
| Spectral Vowel Freeze | Pad | Pad | 1, 6 | None | -11.7 | 19.3 -> 28.1 | TODO |
| Shepard Ascent | Pad | Pad | 1, 1 | None | -5.8 | 62.1 -> 42.1 | TODO |
| Frozen Pluck Drone | Pad | Pad | 1, 3 | Convolve | -7.2 | 77.1 -> 59.9 | TODO |
| Chord Cloud Pad | Pad | Pad | 1, 6 | None | -5.3 | N/A | TODO |
| Infinite Horizon | Pad | Pad | 1, 7 | None | -14.4 | N/A | TODO |
| Throat Singer Drone | Pad | Pad | 13, 0 | None | -12.5 | N/A | TODO |
| Odd Harmonics Pad | Pad | Pad | 8 | None | -20.2 | N/A | TODO |
| Subtle Tape Drift | Pad | Pad | 5 | None | -16.4 | N/A | TODO |
| Golden Dawn Pad | Pad | Pad | 0, 3 | None | -19.7 | N/A | TODO |
| Frozen Aurora | Pad | Pad | 10 | None | -16.4 | N/A | TODO |
| Lush Analog Pad | Pad | Pad | 1, 5 | None | -19.8 | N/A | TODO |
| Hyper Dimension Pad | Pad | Pad | 6 | None | -13.2 | N/A | TODO |
| Parallel Dusk Pad | Pad | Pad | 1, 8 | None | -21.9 | 88.2 -> 41.1 | TODO |
| Comb Reso Swell | Pad | Pad | 1 | None | -13.4 | N/A | TODO |
| Brass Swell Ensemble | Pad | Pad | 1, 1, 1 | None | -16.4 | 65.1 -> 45.3 | TODO |
| Tri-Band Split Texture | Pad | Pad | 1 | None | -18.8 | N/A | TODO |
| Vintage Brass Pad | Pad | Pad | 1 | None | -16.5 | N/A | TODO |
| Velvet Strings | Pad | Pad | 1 | None | -17.0 | N/A | TODO |
| Bowed MPE Strings | Pad | Pad | 1, 1 | None | -10.9 | 73.9 -> 60.1 | TODO |
| Space Odyssey Pad | Pad | Pad | 6 | None | -16.5 | N/A | TODO |
| Diffusor Cloud Pad | Pad | Pad | 1 | None | -13.5 | 45.5 -> 29.5 (-16.0) | TODO |
| Slow Motion Waves | Pad | Pad | 5 | None | -12.6 | N/A | TODO |
| Harmonic Cloud | Pad | Pad | 7 | None | -15.6 | N/A | TODO |
| Deep Sea Ambient | Pad | Pad | 3 | None | -12.2 | N/A | TODO |
| Vowel Choir Pad | Pad | Pad | 9 | None | -17.1 | N/A | TODO |
| Bowed Pressure Strings | Pad | Pad | 1, 4 | None | -10.7 | N/A | TODO |
| Lorenz Garden | Pad | Pad | 12, 7, 9 | None | -17.5 | 96.1 -> 61.7 | TODO |
| Ethereal Glass | Pad | Pad | 12 | None | -11.6 | N/A | TODO |
| Spectral Gong Pad | Pad | Pad | 1, 12 | None | -1.8 | 20.3 -> 27.3 | TODO |
| Radiant Pulse Pad | Pad | Pad | 4 | None | -16.8 | N/A | TODO |
| Sync Cry Lead | Lead | Lead | 1, 1 | None | -12.8 | N/A | TODO |
| Staccato Bleep Lead | Lead | Lead | 2 | None | -7.9 | N/A | TODO |
| Harmonic Shimmer Lead | Lead | Lead | 7 | None | -21.9 | N/A | TODO |
| Breath Flute Lead | Lead | Lead | 6, 1 | None | -17.4 | N/A | TODO |
| Screaming Sync Lead | Lead | Lead | 1 | None | -17.4 | N/A | TODO |
| Metallic Edge Lead | Lead | Lead | 11 | None | -15.8 | N/A | TODO |
| Plucked Lead Tone | Lead | Lead | 1 | None | -8.9 | N/A | TODO |
| Triple Fifth Lead | Lead | Lead | 1, 4, 3 | None | -14.9 | 83.5 -> 52.1 | TODO |
| Warm Vintage Lead | Lead | Lead | 5 | None | -12.3 | N/A | TODO |
| Chorused Saw Solo | Lead | Lead | 1 | None | -18.8 | N/A | TODO |
| Tri Soft Solo | Lead | Lead | 3 | None | -13.3 | N/A | TODO |
| Cosmic Drift Lead | Lead | Lead | 7 | None | -16.5 | N/A | TODO |
| Diode Bite Lead | Lead | Lead | 1 | None | -4.4 | N/A | TODO |
| Vowel Scrub Choir | Lead | Lead | 1, 13 | Convolve | -12.2 | 47.3 -> 33.5 (-13.7) | TODO |
| Hyper Saw Lead | Lead | Lead | 1, 1 | None | -18.1 | N/A | TODO |
| Laser Beam Lead | Lead | Lead | 1 | None | -14.6 | N/A | TODO |
| Dirty Peak Lead | Lead | Lead | 4 | None | -23.8 | N/A | TODO |
| MPE Pressure Lead | Lead | Lead | 1 | None | -10.0 | N/A | TODO |
| Vowel Whistle Lead | Lead | Lead | 1, 3 | None | -19.7 | N/A | TODO |
| Pulse Width Mod Lead | Lead | Lead | 11 | None | -6.7 | N/A | TODO |
| Solo Square Lead | Lead | Lead | 2 | None | -16.7 | N/A | TODO |
| Bell FM Ripple Lead | Lead | Lead | 0, 0, 12 | None | -17.6 | 67.2 -> 43.7 | TODO |
| Vocaloid Lead | Lead | Lead | 9 | None | -21.1 | N/A | TODO |
| Dual Octave Hero | Lead | Lead | 1, 1 | None | -18.9 | N/A | TODO |
| Spectral Bowed Glass | Lead | Lead | 1, 13 | None | -15.4 | N/A | TODO |
| FM Digital Lead | Lead | Lead | 10 | None | -14.7 | N/A | TODO |
| Twin Warp Lead | Lead | Lead | 1 | None | -15.4 | N/A | TODO |
| Bell Chime Lead | Lead | Lead | 12 | None | -13.2 | N/A | TODO |
| Phaser Sweep Lead | Lead | Lead | 6 | None | -11.6 | N/A | TODO |
| Vowel Table Lead | Lead | Lead | 13, 3 | None | -16.0 | N/A | TODO |
| Breakbeat Slice Keys | Keys | Keys | 1, 0 | Convolve | -3.5 | N/A | TODO |
| Vibraphone Bars | Keys | Keys | 0 | None | -17.7 | N/A | TODO |
| Muted Honky Tonk | Keys | Keys | 1 | None | -8.5 | N/A | TODO |
| Vintage Electric Tine | Keys | Keys | 0, 12 | None | -17.2 | N/A | TODO |
| Dream Grand Piano | Keys | Keys | 3, 7 | None | -16.4 | N/A | TODO |
| Utility Width Keys | Keys | Keys | 3, 2 | None | -17.2 | N/A | TODO |
| Just Intonation Keys | Keys | Keys | 7 | None | -12.7 | N/A | TODO |
| Dyno Electric Piano | Keys | Keys | 11 | None | -4.9 | N/A | TODO |
| Warm Toy Piano | Keys | Keys | 2 | None | -9.7 | N/A | TODO |
| B3 Drawbar Organ | Keys | Keys | 7 | None | -14.5 | N/A | TODO |
| Tine Glass Keys | Keys | Keys | 0, 0, 12 | None | -14.7 | 65.1 -> 43.4 | TODO |
| Marimba Layer Keys | Keys | Keys | 1, 0 | Convolve | -8.7 | 54.5 -> 36.0 | TODO |
| Lo-Fi Rhodes | Keys | Keys | 3 | None | -12.2 | N/A | TODO |
| Drawbar Organ Keys | Keys | Keys | 13, 0 | None | -17.3 | N/A | TODO |
| Accordion Reed | Keys | Keys | 4, 4 | None | -19.8 | N/A | TODO |
| Clavinet Funk | Keys | Keys | 2 | None | -16.8 | N/A | TODO |
| Rotary Rock Organ | Keys | Keys | 8 | None | -24.6 | N/A | TODO |
| Dusty Rhodes Keys | Keys | Keys | 13, 12, 1 | Convolve | -18.2 | 61.1 -> 40.2 | TODO |
| Glass Zone Bells | Keys | Keys | 1, 12 | Convolve | -10.1 | 52.0 -> 35.4 (-16.6) | TODO |
| Tempo Gate Chords | Keys | Keys | 1, 1 | None | -12.9 | N/A | TODO |
| FM Soft Organ | Keys | Keys | 10 | None | -13.5 | N/A | TODO |
| Glass Organ Tones | Keys | Keys | 12 | None | -11.5 | N/A | TODO |
| Harpsichord Bar | Keys | Keys | 4 | None | -7.3 | N/A | TODO |
| Celesta Bells | Keys | Keys | 12 | None | -8.9 | N/A | TODO |
| Wah Pressure Clav | Keys | Keys | 4, 1 | None | -12.3 | 73.2 -> 39.7 | TODO |

# Release Notes

We are proud to introduce a refined selection of factory presets for the first release. This carefully curated set ensures optimal performance and covers a broad range of sonic textures. Each preset in this collection has been thoroughly auditioned to meet our high quality standards. You will find a wide variety of sounds including basses, pads, keys, and effects to jumpstart your music production. This reduced set offers a more streamlined browsing experience while maintaining full creative versatility. We hope you enjoy exploring these high quality sounds in your projects.
