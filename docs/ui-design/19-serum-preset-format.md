# 19 - The Serum 2 file format (reverse engineered, WP-21)

Source: the 1283 real files of a Serum 2 installation (656 `.SerumPreset`, 220 effect chains, 169 shapes and curves, 137 clips, ...), read by `core/serum-import` and
checked by `scripts/audit-serum-import.py` with the reference zstd decoder. Parameter semantics come from the Serum 2 manual (docs/ui-design/_manuals/serum2, not
committed) and from the enumerations that the Serum 2 binary carries as text (parameter order of every module, mod source menu, filter and warp lists). Nothing of the
real content is committed; the tests build their fixtures in code (`core/serum-import/tests/serum_fixtures.h`).

## 1. Container

All integers little endian.

| Offset | Size | Content |
|-------:|-----:|---------|
| 0 | 9 | `XferJson` and a NUL |
| 9 | 8 | N, the length of the header (the old files count a trailing NUL in N, the new ones do not) |
| 17 | N | the header: one JSON object (below), UTF-8 |
| 17 + N | 4 | size of the decoded body in bytes |
| 21 + N | 4 | body format: `2` = zstd + CBOR (all current files: 1271 of the 1283), `0` = zlib + UBJSON (12 old files: 4 `.SerumFX`, 5 `.SerumFXRack`, 3 `.XferPath`, header `version` 2.x / 3.0) |
| 25 + N | rest | the compressed body |

Header members (all files): `fileType`, `hash`, `product`, `productVersion`, `url`, `vendor`, `version`. `.SerumPreset` adds `presetName`, `presetAuthor`,
`presetDescription` (and rarely `presetComment`), `tags` (`Wavetable`, `Sample`, `Multisample`, `Granular`, `Spectral`, `Poly`, `Mono`, `Arp`, `Clip`, `Preview`,
`Embedded-Data`, `KB-Span`, `Custom-Tuning`; no category: the category is the folder of the file). `.SerumFX` adds `fxType`. `hash` is the MD5 of the compressed body
(bytes after the format word), verified on the real files. `version` is the file version (3.0 ... 9.0 in the presets, `productVersion` 2.0.11 ... 2.0.24).

Body of format 2: one zstd frame (RFC 8878; single segment, content size field, no checksum, blocks of at most 128 KiB; the encoder uses raw, RLE and compressed
blocks with Huffman literals and FSE sequences) holding one CBOR value (RFC 8949: definite lengths only, maps with text keys, numbers as half / single / double
floats or integers, arrays of floats for the sample data, no tags). Body of format 0: one zlib stream (RFC 1950) holding one UBJSON value (objects `{` with
length-prefixed keys, `[` arrays, `D` / `d` floats, `i U I l L` integers, `S` strings, `T F`); it is read into the same value tree.

`core/serum-import` implements both decoders itself (`zstd_decode.cpp`, `serum_container.cpp`): no library, every length checked, the output bounded. All 1283
container files decode; the size field of every frame and the xxHash64 checksum (when a frame has one) are verified.

## 2. The body of a `.SerumPreset`

A map with 175 members in the files of version 9. Every module is a member named `<Module><index>`; most hold `plainParams`, either the text `"default"` (every
parameter at the Serum default) or a map of the parameters that are NOT at their default (so a missing parameter means "Serum default", not "unknown"). Values are
plain numbers in the unit of the Serum display (percent, seconds, semitones, Hz, linear gain, or a normalised 0 .. 1 for the filter frequency); choices are strings
(`kPingPong`, `MgL24`, `kHall`).

| Member | Count | Content |
|--------|------:|---------|
| `Oscillator0..4` | 5 | 0 .. 2 = OSC A, B, C; 3 = noise; 4 = sub. `plainParams` (enable, volume, pan, octave, pitch, fine, coarse, unison set, sample player parameters, `kParamType` = `kOsc_WT` (default) / `kOsc_MultiSample` / `kOsc_Sample` / `kOsc_Granular` / `kOsc_Spectral`) plus one block per type: `WTOsc<i>`, `MultiSampleOsc<i>`, `SampleOsc<i>`, `GranularOsc<i>`, `SpectralOsc<i>` (warp, table position, content references), `NoiseOsc3`, `SubOsc4`, and `sliceMarkers` |
| `VoiceFilter0..1` | 2 | `plainParams` (enable, `kParamType` = one of 111 type names, frequency 0..1, reso, drive, var, wet, level, stereo, key track, pad, x, y), `PZs` (poles / zeros of the PZ SVF) |
| `Env0..3` | 4 | attack, hold, decay, sustain (linear 0..1), release (seconds), `kParamCurve1..3` (0 .. 100, 50 = straight), beat sync flag |
| `LFO0..9` | 10 | `plainParams` (type `None` / `RandomSH` / `Lorenz` / `Rossler` / `Path`, mode `Free` / `Envelope` / trigger (absent), rate, beat sync, dotted, triplets, 10x, rise, delay, smooth, phase, mono, direction, anchored), `curveData` (`xVals`, `yVals`, `curveVals`, `numPoints`, `loopbackPointNum`), `curveDisplayName`, `pathData` |
| `LFOPointModBus0..15`, `lfoPointModAssignments` | 16 | the modulation of single LFO points: a bus per assignment (`busID`, `lfoID`, `lfoType`, `pointID`, `target` 0 = X, 1 = Y, 2 = curve); the bus is fed by a `ModSlot` whose destination is `LFOPointModBus` |
| `Macro0..7` | 8 | `name`, `plainParams.kParamValue` (0 .. 100) |
| `ModSlot0..63` | 64 | `source` = `[main, aux]` (the menu order below, 0 = none), `destModuleTypeString`, `destModuleID`, `destModuleParamName`, `destModuleParamID` (the index of the parameter in the enumeration of the module), `plainParams` (`kParamAmount` -100 .. 100, `kParamBipolar`, `kParamAuxInverted`, `kParamBypass`, `kParamCurveIn`, `kParamAuxCurve`, `kParamOut`, `kParamCurveOut`, `kParamMainCurveData`, `kParamAuxCurveData`, delay, smooth), `flex` = `[main curve, aux curve]` (empty maps when unused) |
| `FXRack0..2` | 3 | Main, Bus 1, Bus 2: `FX` = array of `{ "<FXType>": { plainParams, ... }, "type": n, "flex": [..], "kUIParamMixOrGain": x }`, `displayName`, `plainParams` |
| `RoutingSlot0..6` | 7 | OSC A, B, C, noise, sub, filter 1, filter 2: `kParamRoutingDest` (`kRoutingDestFilter` / `Master` / `Direct` / `None`), `kParamFilterBalance` (-100 = filter 1 only .. 100 = filter 2 only), `kParamFXBus1Level`, `kParamFXBus2Level`, `kParamViaEnv1` |
| `Global0` | 1 | master volume (linear), poly count, bend ranges, legato, mono, portamento, transpose, tuning, direct and bus volumes / destinations, performance state (mod wheel, program, MIDI out, latch) |
| `VoicePanel0` | 1 | the voice panel: eight steps of pan / detune / env time / cutoff / mod 1 / mod 2, random amounts, scaling |
| `Arp0`, `ArpClip0..11`, `MidiClip0..11`, `ClipPlayer`, `ClipPlayer0` | 26 | the arpeggiator and the clip player: notes, automation lanes, grid state |
| `PitchQuantizer0`, `RetriggerState0` | 2 | the scale quantizer (`scale` = 12 note flags, `scaleName`); the retrigger flags |
| `scalars` | 1 | `velo` and `note`: the drawn velocity / note curves (same curve object as an LFO, plus `legato`) |
| `tuningName`, `tuningData`, `midiMap` | optional | a custom tuning table, the MIDI CC map |
| `mpeEnabled`, `mpePitchBendRange`, `mpeConfig`, `lockTuning`, `lockOversampling` | | |
| `SerumGUI`, `Osc`, `WTOsc`, `GranularOsc`, `SpectralOsc`, `MultiSampleOsc`, `Filter`, `ClipPlayer` (UI form) | | interface state (`kUIParam*`) |
| `presetName`, `presetAuthor`, `presetDescription`, `tags`, `fileType`, `product`, `productVersion`, `url`, `vendor`, `version`, `arpBankDisplayName`, `clipBankDisplayName` | | the header again |

### 2.1 Curves (`curveData`, `scalars.*`, `flex`, `pathData`)

`xVals`, `yVals`, `curveVals` are arrays of `numPoints + 1` values; `numPoints` is the number of segments. The polyline runs through all points; `yVals` is the screen
y (0 = top): the value is `1 - y`. `curveVals[i]` is the bend of the segment that starts at point i: 0.5 straight, below 0.5 it starts fast, above it starts slowly
(measured on the factory `sine` and `triangle` shapes). `loopbackPointNum` is the loop point of an envelope mode LFO. The default curve of an empty `flex` is
`xVals [0, 1]`, `yVals [1, 0]` (the identity).

### 2.2 Embedded and referenced content

* `WTOsc<i>`: `embeddedWTData` (an array of floats: a multiple of 2048 samples = that many frames, at most 256) or `relativePathToWT` (relative to the `Tables` folder,
  a leading slash is allowed); `numFrames` / `numChannels` / `sampleRate` describe the table the oscillator was loaded from, `interpolateAfterLoad` the way the key
  frames were interpolated, `tableDisplayName`, `storedPhasePos`, `flex` (the drawn remap of the table position).
* `SampleOsc<i>` / `GranularOsc<i>` / `SpectralOsc<i>`: `samplePathRelative` (relative to `Samples`), `baseNote`, `numFrames`, `numChannels`, `sampleRate`, `trimHead`,
  `trimTail`, `gain`. `MultiSampleOsc<i>`: `sfzPathRelative` (relative to `Multisamples`), `embedded_sfz` (the SFZ text), `files` (a map of the zone samples with their
  frame counts, keyed by the relative path with either slash), `defaultLoopMode`.
* `NoiseOsc3`: `relativePathToNoiseSample` (under `Samples/Factory Non-Tonal/Noises`), `embeddedNoiseData`, `kParamNoiseType` (`White`, `Pink`, `Brown`, `Geiger`).
* `FXConv` (convolution reverb): `embeddedIR` or `relativePathToIR` (relative to `Impulses`), `numChannels`, `numFrames`, `sampleRate`.
* Paths may climb (`Factory/Drums/../../../Samples/...`): they are relative to the folder of the area and stay inside the Serum content folder.
* Content folders: `Tables/*.wav` (32 bit float, usually 2048 samples per frame), `Samples`, `Multisamples` (`.sfz` + `.flac`), `Impulses` (`.flac`); all `.flac`
  files of the installation decode with `core/serum-import/audio_decode.cpp` bit exactly (compared with `afconvert` on a sample).

### 2.3 Modulation sources (`ModSlot.source`)

Ids are the position in the source menu of Serum 2 (the binary lists the names in this order): 1 Mod Wheel, 2 .. 5 Env 1 .. 4, 6 .. 15 LFO 1 .. 10, 16 Velo, 17 Note#,
18 Aftertouch, 19 Poly Aftertouch, 20 Noise OSC, 21 Note-On Rand 1, 22 Rand 2, 23 Alt. 1, 24 Alt. 2, 25 .. 32 Macro 1 .. 8, 33 Pitch Bend, 34 Expr X, 35 Expr Y,
36 Expr Z, 37 Release Velo, 38 Fixed, 39 .. 48 LFO 1 .. 10 Y, 49 .. 51 OSC A .. C, 52 SUB OSC, 53 Filter 1, 54 Filter 2, 55 Active Voices, 56 Voice Mod 1,
57 Voice Mod 2, 58 Voice Index, 59 Note-On Rand (Discrete). The second element is the aux source, same ids.

### 2.4 Modulation destinations

`destModuleTypeString` is the module (`Oscillator`, `WTOsc`, `SampleOsc`, `MultiSampleOsc`, `GranularOsc`, `SpectralOsc`, `NoiseOsc`, `VoiceFilter`, `Env`, `LFO`,
`Macro`, `Global`, `RoutingSlot`, `LFOPointModBus`, `Arp`, `MidiClip`, `VoicePanel`, `ModSlot`, `FX<Type>`), `destModuleID` the instance (oscillator / filter / envelope /
LFO / macro index; for an effect `rack * 100 + position in the rack`), `destModuleParamName` the parameter (`kParamVolume`).

### 2.5 Effects

`FXBode`, `FXChorus`, `FXComp`, `FXConv`, `FXDelay`, `FXDistortion`, `FXEQ`, `FXFilter`, `FXFlanger`, `FXHyperD`, `FXPhaser`, `FXReverb`, `FXUtils` and the splitters
`FXSplit` (L/H), `FXSplit3` (L/M/H), `FXSplitMS`, which carry `kParamModuleCount1..3` (how many of the following modules sit in each band). Extra members of a module:
`lfophasor` (runtime state of the LFO), `PZs` (the PZ filter), `lfo` (HyperDimension), the impulse response fields above, `flex` (the two drawn X-Shaper curves of the
distortion).

## 3. The other file types

| Extension | Body | Content | Importer |
|-----------|------|---------|----------|
| `.SerumFX` | `{ "FX<Type>": {...}, "fxType", "type" }` | one effect module | effect chain: pool slot |
| `.SerumFXRack` | `{ "FXRack": { "FX": [...], "plainParams" } }` | a rack | effect chain: pool slots |
| `.XferShape` | `{ "LFO": curve }` | an LFO shape (folder `LFO Shapes`) or a curve (folder `Curves`) | `.lfoshape.json` |
| `.XferPath` | `{ curveVals, xVals, yVals, isOpen, numPoints }` | an XY path of a path LFO | reported, not modelled |
| `.XferClip`, `.XferClipBank`, `.XferArp`, `.XferArpBank` | note lists with expression lanes | clips and arpeggiator patterns | reported, not modelled |
| `.SerumStyle` | `{ "VoicePanel0": ... }` | a voice panel style | reported, not modelled |
| `.XferFilterPZSingle`, `.XferFilterPZMulti` | `{ "PZ": { fp, fz, qp, qz, gain, types } }` | poles / zeros of the PZ SVF | reported, not modelled |
| `.SerumMIDIMap` | `{ "midiMap": [ { ccNum, paramIDs } ] }` | a MIDI CC map (`paramIDs` = module * 1,000,000 + index * 1,000 + parameter) | reported, not modelled |
| `.flac` / `.wav` below `Impulses` | audio | an impulse response | stored in the library |

## 4. What the reverse engineering could not settle

The units that the file does not state and the manual does not give are listed with their defaults in `13-new-keys.md` 4.26 (assumptions A325 ..) and in the morning
questions 54 ..: the filter frequency scale, the LFO rate in tempo mode, the curve bend scale, the oscillator level law, the effect parameter ranges of the reverb and
compressor, the point modulation targets, and the key frame interpolation of embedded tables.

Update (REFCOMPARE, `13-new-keys.md` 4.46): with the Serum 2.1.5 plug-in installed, the units listed above were measured; the filter frequency scale (+2 semitones on the 135 note law), the
oscillator / master / sub / noise level law (linear gain, defaults 0.5625 / 0.25 / 0.5625 / 0.0625), the envelope curve knobs, the compressor threshold, the wavetable position (position / 256) and the
unison level are no longer assumptions.

## 5. The plug-in state of Serum 2 (VST3)

Found while loading presets into the real plug-in (`tools/serum-reference`): `getStateInformation` of the VST3 returns a JUCE wrapper (`VC2!`, the size of the text, an XML text `<VST3PluginState>` with the
elements `IComponent` and `IEditController`, each the JUCE base64 text `<size>.<data>` of the plug-in's own state, a terminating NUL). Both states are XferJson containers like a preset file (section 1), with the
header member `component` (`processor` / `controller`) in place of `fileType`, `product`, `productVersion`, `version` (11.0 in 2.1.5), `url`, `vendor`, `hash` (MD5 of the compressed body); the controller
header also has `presetName`, `presetAuthor`, `presetComment`, `presetDescription`. The processor body is the preset body restricted to its 164 processor members (no `Osc`, `WTOsc`, `GranularOsc`,
`MultiSampleOsc`, `SpectralOsc`, `Filter`, `ClipPlayer` (UI form), `SerumGUI`, `presetName`, `presetAuthor`, `presetDescription`, `fileType`, `arpBankDisplayName`, `clipBankDisplayName`; plus
`modMatrixLocked` and `scalarCurvesLocked`), the controller body holds the interface members and copies of several processor members (LFO, FXRack, clips). A processor state that still has the interface members is refused without a message; an older `version` in the header
is accepted (the plug-in migrates). The body may be stored as a zstd frame of raw blocks.
