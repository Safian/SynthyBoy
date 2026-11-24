# 15 - Shell features: MIDI learn (G07), Save as Default (G02), module Lock (G08), Vital bank reading

Spec of the key-free shell features (no new parameter keys, no table block, key count stays 1484). Assumption numbers A180 .. A196.
Code: `core/include/synthyboy/shell_features.h` (+ `core/src/shell_features.cpp`), `shell-juce/PluginProcessor.*`, `shell-clap/synthyboy_clap.cpp`,
`core/include/synthyboy/zip_reader.h`, `core/vital-import/vital_batch.cpp`, `tools/vital-import/vital_import_cli.cpp`.

## 1. Where the state lives

None of the three features is a parameter. They are stored in the additive **`shell` section** of the host state (the same JSON document as the
parameters and the `content` section; older readers ignore it, `loadState` skips it):

```
{"format":"synthyboy-state","version":3,"params":{...},"content":{...},"shell":{"midi_cc":[{"cc":21,"key":"filter1.cutoff"}],"locks":["osc1"]}}
```

- A180: the state version stays 3 (additive). The shells always write the section, even empty. `.sbpreset` files never carry it: a preset must not change
  the user's controller setup or locks.
- A181: **presence of the section means "this is a session of ours"**, absence means "this is a preset / a patch from elsewhere / a state of an older
  version". That decides whether locks are applied on load (section 4).
- Caps on load (`loadShellSection`): at most 128 MIDI entries, 64 locks, 48 bytes per lock name, key names 96 bytes; a section outside the caps is
  dropped as a whole (`ShellStatus::rejected`), the parameters are still applied. Unknown members are skipped, entries with an unknown key are skipped.

## 2. MIDI learn (G07)

- A182: one CC per parameter and one parameter per CC (`MidiCcMap`, 128 atomics, audio-thread safe); **any channel** (omni).
- A183: not learnable: 0 and 32 (bank select), 64 (sustain pedal), 6 / 38 / 96-101 (data entry, RPN / NRPN), 120-127 (channel mode). **CC 1 (mod wheel) and
  CC 74 (slide) can be learned**; the engine keeps its own use of them as well (the message still goes through `translateMidiMessage`).
- A184: value mapping = CC / 127 on the **normalised** range of the parameter (the table's skew applies), then `constrainValue` (stepped / interval
  parameters snap). 7 bit only: no 14-bit pairs, no NRPN.
- A185: learn mode arms **one key**; the next learnable CC is mapped and does **not** move the knob that time. Arming another key replaces the first; learn
  mode is not saved. A learned CC that was mapped to another parameter moves.
- A186 (shell-juce): a mapped CC sets the JUCE parameter with `setValueNotifyingHost` (host automation sees it, the knob follows) and the engine in the same
  block (`rawParams` exact value, `sentToEngine` updated). No allocation: the parameter pointers are cached at construction.
- A187 (native CLAP shell): the mapped CC (`CLAP_EVENT_MIDI`) sets the value and is reported to the host with a `CLAP_EVENT_PARAM_VALUE` on `out_events`.
  The native shell has **no editor**, so it cannot learn: it uses the map of the state / of the user default (which the editor shell writes). Kick's remote
  control pages are untouched.
- UI (touch-first): long-press (or right click) any control -> parameter sheet -> **MIDI learn** (becomes **Cancel learn** while armed, **Remove CC n** when
  mapped). The sheet closes on arming; the knob shows an amber `LEARN` frame, a mapped knob shows a small `CCn` tag (`ModRing`, `Overlays.cpp`). The
  editor timer polls `UiHost::midiMapRevision()` so a CC learned on the audio thread redraws the knobs.

## 3. Save as Default (G02)

- A188: the default is one file `default.sbstate` in the **user data directory** (`userDataDirectory()`: `$SYNTHYBOY_USER_DIR` if set, else
  `~/Library/Application Support/SynthyBoy`, `%APPDATA%\SynthyBoy`, `$XDG_DATA_HOME` or `~/.local/share/SynthyBoy`). Written atomically (temp file + rename).
  Content = the full state document including the `content` and `shell` sections.
- A189: a **new instance** of either shell starts from it (the host's state, if any, replaces it afterwards); "Revert Saved" of a new instance is the
  default. A missing or damaged file is ignored (factory init patch).
- A190: **Init Preset** (Global Settings) loads the default's parameters and content (not its MIDI map and locks), or the factory init patch when there is
  none. Buttons: **Save as Default**, **Clear Default** (enabled only while a default exists).

## 4. Lock (G08)

- A191: a lock entry is a **module prefix** (`osc1`, `filter2`, `env3`, `lfo4`, `rlfo1`, or any valid prefix such as `fx`... the UI offers the modules of the
  module sheet) or a **parameter key**. It covers every key `<entry>.*` (resp. that key) and, for `oscN` / `lfoN` / `filterN`, the content of that module
  (wavetable / sample / remap reference, custom LFO shape, drawn filter curve).
- A192: the modulation slots (`modNN`) are **not** locked: modulations that point at or come from a locked module follow the preset. (Question 21.)
- A193: locks are **applied on a preset change**: `UiHost::loadPatch` (the entry point for Kick's preset browser), `initPatch`, `setStateInformation` with a
  document without the `shell` section, the same in the native CLAP shell. They are **not applied** on a session restore (state with the `shell` section:
  restored exactly, locks included), "Revert Saved", the load of the user default by a new instance, and explicit edits (Init module, paste, swap, knobs).
- UI: module sheet (long-press the header or the `...` button) -> **Lock module** / **Unlock module**; a locked header button reads `LOCK`, the sheet title
  shows `(locked)`.

## 5. Vital banks and stand-alone files (`.vitalbank`, `.vitaltable`, `.vitallfo`)

- A194: `zip_reader` (own stored + inflate implementation, written from RFC 1951, no third-party code): central directory, methods 0 and 8, CRC-32 checked on
  every entry. Refused: encryption, zip64, other methods, multi-disk. Limits: 5000 entries, 32 MiB per entry, 1 GiB declared in total, 1024 bytes per name;
  an entry is inflated into a buffer of exactly its declared size and the stream must end there (a bomb cannot write more).
- A195: names with `..`, a leading `/`, a drive letter, a back slash or control characters are not read (the row says `unsafe path`); directories and other files
  (readme, images) are `ignored` rows. Entries of any folder depth go to one folder named after the bank (`<Bank>/<name>.sbpreset`); two different presets
  with one name keep both (`Name (2)`), the same source is skipped (`src:<12 hex>` tag), `--duplicates replace` writes again.
- A196: a stand-alone `.vitaltable` is stored in the content pack (identity = SHA-256 of the WAV, never twice); a stand-alone `.vitallfo` is written as
  `<name>.lfoshape.json` (`{"format":"synthyboy-lfo-shape","version":1,"name","smooth","points":[{"x","y","power"}]}`), there is no UI consumer of that file yet
  (Question 22).
- CLI: `synthyboy_vital_import --in FILE_OR_DIR [--out DIR] [--content-dir DIR] [--report] [--duplicates skip|replace|keep]`; `--in` is a `.vital`,
  `.vitalbank`, `.vitaltable`, `.vitallfo` or a directory of them (recursive).
