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

## Source included in this repository

Only in `core/engine-vital/` (built with `SYNTHYBOY_WITH_VITAL=ON`, never in iOS builds; see `docs/UPSTREAM.md` for the exact
file list, the changes and what was not taken).

| Component | Revision | Licence (as used here) | Files |
|-----------|----------|------------------------|-------|
| Vital (Matt Tytel) | commit `636ca0e` (2022-04-20), https://github.com/mtytel/vital | GPL-3.0-or-later (every imported file's header: "version 3 ... or (at your option) any later version"); the combined work is AGPL-3.0-or-later via GPLv3 section 13 | `core/engine-vital/upstream/src/synthesis/**` (143 files after removing `effects_engine`), `src/common/wavetable/**`, `src/common/{synth_constants.h, synth_types.*, synth_parameters.*, line_generator.*, tuning.*, fourier_transform.h}`, `src/interface/look_and_feel/synth_strings.h` (string table only), licence text `LICENSE-GPL-3.0`; translated tests in `core/engine-vital/tests/synthesis/**` |
| KISS FFT | as shipped in Vital 636ca0e | BSD-3-Clause | `core/engine-vital/upstream/third_party/kissfft/{kissfft.h, COPYING, LICENSES/}` |
| JSON for Modern C++ (nlohmann/json) | 3.1.2, as shipped in Vital 636ca0e | MIT | `core/engine-vital/upstream/third_party/json/json.h` |

Vital's presets, wavetables, skins, tunings, fonts, icons, GUI and network code are not included and must not be added.

JUCE itself bundles further third-party code inside its modules (for example HarfBuzz in
`juce_graphics`, libpng and libjpeg image codecs, FLAC in `juce_audio_formats`); their licences are
listed in JUCE's own `LICENSE.md` and the respective module directories, and are not repeated here.
The pinned JUCE version is built from source; nothing of it is copied into this repository.

## Tools used for verification only (not linked, not distributed)

| Tool | Version | Licence | Used for |
|------|---------|---------|----------|
| pluginval | v1.0.4 (release assets pinned by SHA-256 in `.github/workflows/build.yml`) | GPL-3.0 | VST3 validation |
| clap-validator | 0.4.1 (built from source with cargo) | MIT | CLAP validation |
| GitHub actions `checkout` v7.0.1, `upload-artifact` v7.0.2 | pinned by commit SHA | MIT | CI |

VST is a registered trademark of Steinberg Media Technologies GmbH.
