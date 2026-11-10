# Third-party software

SynthyBoy is licensed under AGPL-3.0-or-later. It is built against the components in the first table, which are
fetched at configure time by CMake `FetchContent` at the exact revisions given (see `CMakeLists.txt`), and it contains
the source files listed in the second table, which are copied into this repository.

| Component | Version / revision | Licence (as used here) | Source |
|-----------|--------------------|------------------------|--------|
| JUCE | tag `8.0.17-security` (commit `dabd5ad13cd869cba41046947fffc8e5ce5cd662`) | AGPL-3.0 (JUCE is dual-licensed AGPLv3 / commercial; the AGPLv3 option is used) | https://github.com/juce-framework/JUCE |
| clap-juce-extensions | commit `7adee3a1bd4684d4caa5601100e364abccff4b4d` (`0.26.0-117-g7adee3a`, 2026-10-06; the last tag, 0.26.0, dates from 2022) | MIT | https://github.com/free-audio/clap-juce-extensions |
| CLAP (`clap-libs/clap`, submodule of clap-juce-extensions) | 1.2.7 (`29ffcc273be7c7c651f6c9953b99e69700e2387a`) | MIT | https://github.com/free-audio/clap |
| clap-helpers (`clap-libs/clap-helpers`, submodule) | `a61bcdf0ecc2c8db1e80bfe8bf9cb7e8d9fd2bbc` | MIT | https://github.com/free-audio/clap-helpers |
| Steinberg VST3 SDK (bundled inside JUCE) | 3.8.0 | MIT | https://github.com/steinbergmedia/vst3sdk |

One patch is applied to a fetched component: `third_party/patches/clap-juce-extensions-extension-hook.patch` (MIT, the licence of the patched files) gives the wrapper of clap-juce-extensions a hook for a CLAP
extension of the plug-in (`clap_juce_audio_processor_capabilities::clapPluginExtension`, called by the wrapper's `extension ()`), so that the JUCE-wrapped CLAP build offers `synthyboy.content` like the native shell. The
top-level `CMakeLists.txt` applies it with `patch -p1` to a copy of the fetched sources in the build directory; the fetched tree is not modified.

## Source included in this repository

Only in `core/engine-vital/` (built with `SYNTHYBOY_WITH_VITAL=ON`, never in iOS builds; see `docs/UPSTREAM.md` for the exact
file list, the changes and what was not taken).

| Component | Revision | Licence (as used here) | Files |
|-----------|----------|------------------------|-------|
| Vital (Matt Tytel) | commit `636ca0e` (2022-04-20), https://github.com/mtytel/vital | GPL-3.0-or-later (every imported file's header: "version 3 ... or (at your option) any later version"); the combined work is AGPL-3.0-or-later via GPLv3 section 13 | `core/engine-vital/upstream/src/synthesis/**` (143 files after removing `effects_engine`), `src/common/wavetable/**`, `src/common/{synth_constants.h, synth_types.*, synth_parameters.*, line_generator.*, tuning.*, fourier_transform.h}`, `src/interface/look_and_feel/synth_strings.h` (string table only), licence text `LICENSE-GPL-3.0`; translated tests in `core/engine-vital/tests/synthesis/**` |
| KISS FFT | as shipped in Vital 636ca0e | BSD-3-Clause | `core/engine-vital/upstream/third_party/kissfft/{kissfft.h, COPYING, LICENSES/}` (the VLA of the original is replaced for MSVC, see docs/UPSTREAM.md) |
| JSON for Modern C++ (nlohmann/json) | 3.1.2, as shipped in Vital 636ca0e | MIT | `core/engine-vital/upstream/third_party/json/json.h` |

Vital's presets, wavetables, skins, tunings, fonts, icons, GUI and network code are not included and must not be added.

JUCE itself bundles further third-party code inside its modules (for example HarfBuzz in
`juce_graphics`, libpng and libjpeg image codecs, FLAC in `juce_audio_formats`); their licences are
listed in JUCE's own `LICENSE.md` and the respective module directories, and are not repeated here.
The pinned JUCE version is built from source; nothing of it is copied into this repository.

## Our own code (no third-party source)

These packages are written for this project (AGPL-3.0-or-later, headers say so) and contain no third-party source. Where an algorithm is a published method, the
implementation is ours; the manuals of other products were used as a description of features, not copied (each package REPORT says so).

| Path | Content |
|------|---------|
| `core/tuning/` | `.scl`, `.kbm`, `.tun` parsers, 128-key table, the `ITuningSource` seam for MTS-ESP (the library is not linked, see below) |
| `core/formula/` | formula compiler (bytecode) and frame renderer; grammar written for this project (`GRAMMAR.md`) |
| `core/shapes/` | custom LFO shapes, velocity / note curves |
| `core/wavetable-edit/`, `core/wavetable-render/` | wavetable data model and tools, clean-room renderer of the ten Vital component types |
| `core/vital-import/` | `.vital` / `.vitaltable` / `.vitallfo` importer (own JSON reader, see the next section for the control-name table) |
| `core/sample-osc/` | sample oscillator DSP and WAV reader |
| `core/filters-serum/`, `core/fx-serum-a/`, `core/fx-serum-b/` | additional filter types and effects; `fx-serum-b` has its own radix-2 FFT (`fft.cpp`) and partitioned convolution |
| `core/src/sha256.cpp` | SHA-256 (RFC 6234), own implementation |
| `core/tests/fuzz_smoke.cpp`, `tools/preset-render/` | fuzz smoke test and the factory preset render gate |

## Vital preset file format (interoperability data)

`core/vital-import/vital_map.cpp` contains a table of the setting names of the `.vital` preset file format
(730 rows, owner decision Q33 in `docs/ui-design/12-decisions.md`). The names are facts of the file format, authored from
our own adapter table (`core/engine-vital/adapter/vital_bindings.cpp`); no Vital preset, wavetable or LFO file is
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

## Tools used for verification only (not linked, not distributed)

| Tool | Version | Licence | Used for |
|------|---------|---------|----------|
| pluginval | v1.0.4 (release assets pinned by SHA-256 in `.github/workflows/build.yml`) | GPL-3.0 | VST3 validation |
| clap-validator | 0.4.1 (built from source with cargo) | MIT | CLAP validation |
| GitHub actions `checkout` v7.0.1, `upload-artifact` v7.0.2 | pinned by commit SHA | MIT | CI |

VST is a registered trademark of Steinberg Media Technologies GmbH.
