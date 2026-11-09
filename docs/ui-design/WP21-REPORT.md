# WP-21 - Serum 2 preset import

Branch `work-wp21` (from develop). Spec: `13-new-keys.md` 4.26 (assumptions A325 .. A346), the container: `19-serum-preset-format.md`, morning questions 54 .. 64, backlog rows S1 .. S13 in `17-BACKLOG.md`. **No new keys** (A343), no state change, `kMaxParams` untouched, `gen-factory-presets.py --check` passes (112 presets).

## 1. What was built

| Part | Files | One line |
|------|-------|----------|
| Format | `docs/ui-design/19-serum-preset-format.md` | the container (header JSON, size, format word, zstd + CBOR / zlib + UBJSON), every module of the body, the source and destination ids, the content references, the other file types |
| Decoders | `core/serum-import/zstd_decode.cpp` (RFC 8878: raw / RLE / compressed blocks, Huffman, FSE, repeat offsets, xxHash64), `serum_container.cpp` (header, inflate, UBJSON), `cbor_reader.cpp`, `audio_decode.cpp` (FLAC, WAV) | no library; all 1283 container files and 5202 audio files of the real installation decode, FLAC bit exact against `afconvert` |
| Importer | `serum_import.cpp` (context, rules engine, global, meta), `serum_voice.cpp` (oscillators, noise, sub, routing, filters, envelopes, LFOs), `serum_content.cpp` (tables, samples, multisamples, impulse), `serum_fx.cpp` (racks, matrix, curves, sweep), `serum_map.cpp` (all tables, enumerations, the 111 filter names, the 59 sources), `serum_api.cpp` | one pass over the value tree; every leaf is imported / approximated / dropped / ignored exactly once |
| Public API | `core/include/synthyboy/serum_import.h`, `serum_plugin_import.h`; `serum_plugin_import.cpp` | `importSerumFile`, `importSerumImpulse`, `serializeSerumPreset`, `importSerumFileForPlugin` (content store glue), `SerumFolderResolver` |
| CLI | `tools/serum-import/serum_import_cli.cpp` (`synthyboy_serum_import --in FILE_OR_DIR --out DIR [--serum-root DIR] [--summary FILE.json] [--report] [--only EXT]`) | `.sbpreset` + `.report.json` per preset / chain, `.lfoshape.json` per shape / curve, impulses into the library, the aggregated summary |
| Audit | `scripts/audit-serum-import.py` | independent: reference zstd, own CBOR, own table of the conversions; 5 checks (completeness, correctness, stray writes, content, matrix) and a mutation self test |
| Plugin | `shell-juce/ui/Editor.cpp:349` (`importSerumFromMenu`), `:340` (`importSerumFile`), `:402` (menu item), `ContentIO.cpp:438` (`importSerumFile`), `ContentSheets.cpp:145` (report sheet) | menu "Import Serum 2 file...": a preset replaces the patch, an effect chain is written onto it, a shape goes to the chosen LFO |
| Tests | `core/serum-import/tests/test_serum_container.cpp` (9 cases), `test_serum_import.cpp` (16 cases, fixtures in `serum_fixtures.h`), `shell-juce/ui/tests/test_ui_content.cpp` (`content_serum_import_inside_the_plugin_with_the_report_sheet`) | synthetic fixtures built in code; the gated tests run the real files when `tests/serum-presets` exists (or `SB_SERUM_PRESETS_DIR`) |

What maps (details in 4.26): oscillators A / B / C (wavetable with the table stored in the library, sample, multisample reduced to one zone, granular and spectral played as a sample), unison, warp 1 / 2 with the Serum warp list, pitch, phase, key / velocity zones, sample player (start / end / loop / scan / slices / reverse), sub, noise, the routing of every channel and the bus sends, the two filters (111 types -> 80), 4 envelopes with curves, 10 LFOs (drawn shapes with bends, Hz / tempo rates, modes, Rise / Delay / Smooth; Lorenz, Rossler and RandomSH become Random LFOs), 8 macros with names, the 64 slot matrix (59 sources, aux source, curves, switches, effect destinations, LFO point modulation), velocity / note curves, the three effect racks as pool slots (13 effect types, two splitters), global voice settings, MPE, the scale quantizer.

## 2. Numbers

Release build, engine vital. Source: the 1283 container files of the installation, 656 of them `.SerumPreset` (copied, gitignored).

**Bulk run (`synthyboy_serum_import` over the whole tree, 1392 inputs = 1283 containers + 109 impulses):** 1392 import, **0 fail, 0 crash or hang** (slowest file 0.18 s, whole tree 17 s), 0 unaccounted leaves of 5,052,751. By family: 656 presets, 163 `.SerumFX`, 57 `.SerumFXRack`, 169 shapes / curves (all leaves imported), 109 impulses (stored), and 238 files of the families without an equivalent (21 paths, 137 clips, 14 clip banks, 12 arps, 4 arp banks, 18 styles, 31 PZ filters, 1 MIDI map) that are read, accounted and reported.

**The 656 presets:** 4,737,615 leaves = 3,571,416 imported + 18,534 approximated + 966,999 dropped + 180,666 ignored (87,617 Serum defaults applied for absent parameters); fidelity close 2 / degraded 654 (every preset has something that sounds dropped: the noise sample file in 482, a source or destination without a home in 152 / 137 ...). 2110 library files stored (tables, samples, impulses), 0 missing referenced files. 89 % of the dropped leaves are the clip player and the arpeggiator (MIDI notes and automation lanes: 86.2 % + 2.9 %), 8.1 % one embedded noise sample, the rest (2.9 %) the items below; the sound-relevant drops are listed in 17-BACKLOG.md S3 .. S13.

Top dropped (files, values): clip player notes 636 / 684,713 and automation 390 / 137,536 (S1); embedded noise sample 1 / 78,241 (S7); arpeggiator 218 / 25,926 (S2); tuning table 2 / 6,960 (S11); PZ filter 14 + 7 / 3,936 (S4); modulation destinations without a key 137 / 2,456 and sources without a home 152 / 2,321 (S8 / S9); path LFOs 39 / 1,051 (S5); spectral flex curves 43 / 830 (S6). Top approximated: the drawn X-Shaper curves of the distortion 97 files, point modulation 74, tempo LFO rates 416, distortion drive scale 351, multisample reduced to one zone 159 (493 oscillators), compressor single band 328, random LFO slots 252, dB depth limit 278.

**Audit (`python3 -I scripts/audit-serum-import.py --serum ... --imported ... --selftest`):** 656 files, 222,722 report entries, **0 errors**: the leaves of the decoded tree equal the report in every file; 45,543 voice values and 16,924 effect values recomputed independently equal the `.sbpreset`; 12,810 matrix sources and 12,810 destinations equal the lists of `docs/params.json`; 1,433 stored tables (3,385,344 samples) equal the embedded data to 1 LSB; 476 samples and 201 impulses exist and hash; 0 stray writes (every non-default key is explained); all 7 injected mutations are found.

**Ctest (full, -j8):** 100 % tests passed out of 66 (109 s, run again after the last code change); `synthyboy_test_serum_container` 9 cases, `synthyboy_test_serum_import` 16 cases / 1102 checks (real run: 656 presets, 0 failed, 0 unaccounted, 656 valid patches), `synthyboy_test_ui_content` including the Serum menu path. Zero compiler warnings (the linker's "ignoring duplicate libraries" notes are older).

**Render gate (`synthyboy_preset_render`, medium tier, 656 imported presets, the stored content attached):** 61 of 656 presets fail the gate (9.3 %; the Vital audit had 8 of 35): peak above +3 dBFS 39, tail longer than 12 s 19, quieter than -60 dBFS 4 (`PN - Emotional Upright`, `PL - Zakrium`, `SY - Air of Nostalgia`, `SY - Thoughtful Keys`: levels opened only by modulation), NaN 1. Chord level of the loudest 300 ms: min -92.3, median -24.8, max +2.4 dBFS (sd 12.4 dB); single note median -31.5 dBFS; tail median 1.66 s. The first render of the importer output had 122 failures, of which 48 "silent": the harness found no sample files because its content directory wants the pack names (`user.samples`) as directory names; with the packs linked the sample presets play. The NaN preset (`BA - Electric Bass - Deep Groover`) is stable when any one of the compressor makeup (+13.5 dB on all three bands), the EQ gain or the EQ resonance (63 / 47) is put back to its default: the three together blow up the chain in the effect library, not in the importer (values are inside the legal ranges).

## 3. What the audit and the render found (and was fixed)

| Finding | Fix |
|---------|-----|
| the noise loop default was inverted (`kParamOneShot` default written as 1) - 210 presets had the noise one-shot | Serum default 0 |
| `kParamVoiceAmp` and `kParamMasterVolume` both wrote `master.volume` (the later one in the file won) | VoiceAmp only as a modulation destination |
| the folder resolver refused the `..` of paths like `Factory/Drums/../../../Samples/...` (23 referenced files missing) | lexical resolution inside the content folder |
| the effect chain files swallowed the header keys (-153 unaccounted) | the module map, not the root, is swept |
| a modulation depth of 75 % on a Serum linear gain (filter level) became +54 dB in our dB range (NaN and +25 dB level outliers in the render) | A346: depth limited to 24 dB down / 12 dB up on gain destinations (question 64) |
| the render found 48 silent presets (all sample based) | not the importer: the harness needs the packs as directories (section 2) |

Open (not importer defects): one preset (`BA - Electric Bass - Deep Groover`) drives the effect chain to NaN (compressor makeup +13.5 dB on three bands with the EQ gain +4 / +9 dB at Q 63 / 47 in front of a 12.7 dB distortion); each one alone is stable. It is a numeric limit of the compressor / EQ in `core/fx-serum-*` with legal values, to be fixed there (the output should be bounded). Peaks above +3 dBFS and tails longer than 12 s are properties of the factory presets (hot master volume, 10+ s reverbs, resonant filters) as in `VITAL-IMPORT-AUDIT-REPORT.md`; the level law question (54) decides how much of it is the importer.

## 4. Assumptions and questions

A325 .. A346 in 13-new-keys.md 4.26, questions 54 .. 64 in 14-morning-questions.md. The decisions that need the owner's ears: the oscillator / master level law (54), the filter frequency scale (55), the LFO tempo rate (57), the curve bend (58), the effect parameter scales (61), one zone of a multisample (63).

## 5. Not done (each one a backlog row, none blocks the import)

S1 clip player, S2 arpeggiator, S3 voice panel, S4 PZ SVF poles / zeros, S5 path LFOs, S6 multisample / granular / spectral parameters, S7 user noise sample, S8 matrix extensions, S9 destinations without a key, S10 splitter bands / several IRs, S11 global extras, S12 sample player extras, S13 point curve modulation. The importer reads and accounts all of them today; the rows say what the model needs to take them over.

## 6. How to reproduce

```
cmake -S . -B build && ninja -C build synthyboy_serum_import synthyboy_test_serum_import
./build/tools/serum-import/synthyboy_serum_import --in <serum>/Presets --out OUT --only SerumPreset --summary OUT/summary.json
python3 -I scripts/audit-serum-import.py --serum <serum> --imported OUT --selftest
./build/core/serum-import/synthyboy_test_serum_import        # SB_SERUM_PRESETS_DIR=<serum> to point the gated test at the files
# render: link the packs as directories next to the content (user.samples -> OUT/content/samples, ...) and pass --content-dir
```
