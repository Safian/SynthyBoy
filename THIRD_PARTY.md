# Third-party software

SynthyBoy is licensed under AGPL-3.0-or-later. No third-party source is copied into this repository except the font file below. The MPC plugin is built against the headers of the official MPC plugin SDK (`MPC_SDK_DIR`, not part of this repository).

## Font of the display layer

`shell-mpc/overlay/fonts/TitilliumWeb-SemiBold.ttf` (`OFL.txt` next to it): Titillium Web SemiBold, licensed under the SIL Open Font License 1.1. `shell-mpc/overlay/sdf_font_data.h` holds the distance-field atlas generated from it; the font software keeps its own licence.

## Our own code (no third-party source)

These packages are written for this project (AGPL-3.0-or-later, headers say so) and contain no third-party source. Where an algorithm is a published method, the
implementation is ours; the manuals of other products were used as a description of features, not copied (each package REPORT says so).

| Path | Content |
|------|---------|
| `core/tuning/` | `.scl`, `.kbm`, `.tun` parsers, 128-key table, the `ITuningSource` seam for MTS-ESP (the library is not linked, see below) |
| `core/formula/` | formula compiler (bytecode) and frame renderer; grammar written for this project (`GRAMMAR.md`) |
| `core/shapes/` | custom LFO shapes, velocity / note curves |
| `core/wavetable-edit/`, `core/wavetable-render/` | wavetable data model and tools, clean-room renderer of the ten component types of the `.vitaltable` wavetable format |
| `core/vital-import/` | `.vital` / `.vitaltable` / `.vitallfo` importer (own JSON reader, see the next section for the control-name table) |
| `core/sample-osc/` | sample oscillator DSP and WAV reader |
| `core/filters-serum/`, `core/fx-serum-a/`, `core/fx-serum-b/` | additional filter types and effects; `fx-serum-b` has its own radix-2 FFT (`fft.cpp`) and partitioned convolution |
| `core/src/sha256.cpp` | SHA-256 (RFC 6234), own implementation |
| `core/tests/fuzz_smoke.cpp`, `tools/preset-render/` | fuzz smoke test and the factory preset render gate |

## Vital preset file format (interoperability data)

`core/vital-import/vital_map.cpp` contains a table of the setting names of the `.vital` preset file format
(730 rows, owner decision Q33 in `docs/ui-design/12-decisions.md`). The names are facts of the file format, authored from
the adapter table of the former Vital-derived engine (removed; git history); no Vital preset, wavetable or LFO file is
bundled. The wavetable renderer (`core/wavetable-render/`) is a clean-room implementation from the design document; it
does not contain Vital source code. The table ships in the binary under the AGPL-3.0-or-later of this project.

## MTS-ESP (not linked, not distributed)

`master.tuning.mts_esp` (docs/ui-design/13-new-keys.md 4.7, owner decision Q23) is served through a seam only:
`synthyboy::IEngine::setTuningSource (const tuning::ITuningSource*)` (core/include/synthyboy/engine.h, core/include/synthyboy/tuning.h).
**No MTS-ESP code or header is part of this repository and the core never links the library.** The ODDSound MTS-ESP client
library (`libMTSClient`, https://github.com/ODDSound/MTS-ESP) is, as far as we know, published under a permissive licence (believed to be MIT; **not verified in this task**, see the check below; the MTS-ESP
master library is separate software, and the "MTS-ESP" name is a trademark of ODDSound Ltd). A shell that wants a master
connection links `libMTSClient` itself (an MIT licensed file, compatible with the AGPL-3.0-or-later of this project; its notice goes
into this file at that time, if the licence text confirms it), wraps `MTS_HasMaster ()` / `MTS_NoteToFrequency ()` in an `ITuningSource` and hands the pointer to the
engine. Until a shell does that the key falls back to the scale (the engine behaves as "no master"). **Licence check to do before the
first shell links the library:** read the licence text of the pinned `libMTSClient` revision and record revision and licence here; this
work package did not fetch it, so nothing above is a verified copy of the licence.

## Formats read without a library (own code, nothing copied)

The Serum 2 importer (`core/serum-import`, WP-21) reads Zstandard (RFC 8878), zlib / DEFLATE (RFC 1950, 1951), CBOR (RFC 8949), UBJSON, FLAC (RFC 9639) and RIFF/WAVE
with decoders written from those specifications; no zstd, zlib or FLAC library is linked and no third party source is part of the repository. The reference zstd
command line tool is used by `scripts/audit-serum-import.py` and the tests only to cross-check the decoder (not distributed). The real Serum 2 files used for testing
(`tests/serum-presets`, gitignored) are never committed.

VST is a registered trademark of Steinberg Media Technologies GmbH.
