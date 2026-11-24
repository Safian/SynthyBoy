#!/usr/bin/env bash
# SynthyBoy - fails if a static library contains anything Vital-derived (docs/ARCHITECTURE.md 2.2, iOS rule).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/check-no-vital.sh <libsynthyboy_core.a> [more archives...]
#        scripts/check-no-vital.sh --source <dir> [more dirs...]   (source tree of the own engine, core/engine-own)
#        scripts/check-no-vital.sh --self-test
#
# An archive is rejected if one of its object file names, symbols (nm, demangled) or strings contains
# "sbvital", "Vital" or "vital::". Exit status: 0 clean, 1 found Vital-derived content (or a failed self-test), 2 usage.
#
# --source: a source tree fails if a file has a Vital identifier (sbvital, vital::, an #include of a path with "vital", an identifier that
# starts with Vital / vital_ / vitalX) or a GPL license text (GNU General Public License, GPL-2.0 / GPL-3.0 / LGPL: only the AGPL header
# of this project is allowed). The plain word Vital in a comment (a name of a reference engine, history) is not an identifier and passes.
#
# Environment: CXX (compiler for --self-test, default c++), AR, NM, STRINGS.

set -euo pipefail

pattern='sbvital|Vital|vital::'
# The importer of the Vital FILE FORMAT (core/vital-import: parses .vital / .vitalbank JSON into our own presets, own code, part of the
# core on every platform) is not Vital-derived engine code: its API identifiers (namespace vitalimport, importVital*, isVitalBankName,
# VitalPluginImport, looksLikeVitalPreset, decodeVitalEffectOrder), the names of the content pack it writes ("Imported Vital Wavetables",
# "User-imported wavetables from Vital presets"), the importer's error texts ("not a Vital file", "the bank holds no Vital preset"; the words
# "Vital" + file / preset / bank / table / LFO) and the user-visible reverb type name "Vital" (core/src/param_table.cpp kReverbTypes, a
# string in the parameter table, no engine code) are allowed. Anything else that mentions Vital still fails.
# The UI library of the static plug-in with the bitmap GUI (synthyboy_static_ui, docs/ARCHITECTURE.md 4.3) adds the user interface of that importer: the result type of its flow (VitalFlowResult,
# shell-juce/ui/ContentIO.h), the texts "Vital import" and "(Vital line mapping)". (The editor's layer model is synthyboy::wtedit::layers.)
allowed='kPackImportedVital|vitalimport|importVital|isVitalBankName|VitalPluginImport|looksLikeVitalPreset|decodeVitalEffectOrder|Imported Vital Wavetables|imported wavetables from Vital presets|Vital (file|preset|bank|table|lfo)|^[[:space:]]*Vital$|VitalFlowResult|Vital import|Vital line mapping'
NM="${NM:-nm}"
AR="${AR:-ar}"
STRINGS="${STRINGS:-strings}"

demangled_symbols()
{
    "$NM" -C "$1" 2>/dev/null || "$NM" "$1" 2>/dev/null | c++filt
}

# prints the offending lines (at most a few per category) and returns 1 if there are any
check_archive()
{
    local archive="$1" found=0 hits

    if [[ ! -f "$archive" ]]; then
        echo "check-no-vital: $archive: no such file" >&2
        return 2
    fi

    hits="$("$AR" t "$archive" | grep -E "$pattern" || true)"
    if [[ -n "$hits" ]]; then
        echo "check-no-vital: $archive: object file names match /$pattern/:" >&2
        echo "$hits" | head -n 5 | sed 's/^/    /' >&2
        found=1
    fi

    hits="$(demangled_symbols "$archive" | grep -E "$pattern" | grep -Ev "$allowed" || true)"
    if [[ -n "$hits" ]]; then
        echo "check-no-vital: $archive: symbols match /$pattern/ ($(echo "$hits" | wc -l | tr -d ' ') lines):" >&2
        echo "$hits" | head -n 5 | sed 's/^/    /' >&2
        found=1
    fi

    hits="$("$STRINGS" -a "$archive" | grep -E "$pattern" | grep -Ev "$allowed" || true)"
    if [[ -n "$hits" ]]; then
        echo "check-no-vital: $archive: strings match /$pattern/ ($(echo "$hits" | wc -l | tr -d ' ') lines):" >&2
        echo "$hits" | head -n 5 | sed 's/^/    /' >&2
        found=1
    fi

    return $found
}

# source tree check: prints the offending lines (at most a few per category) and returns 1 if there are any, 2 if the directory is missing
check_source()
{
    local dir="$1" found=0 hits
    if [[ ! -d "$dir" ]]; then
        echo "check-no-vital: $dir: no such directory" >&2
        return 2
    fi
    hits="$(grep -rnE 'sbvital|vital::|#[[:space:]]*include[[:space:]]*["<][^">]*vital|\bVital[A-Z][A-Za-z0-9_]*|\bvital_[a-z]|\bvital[A-Z][A-Za-z0-9_]*' "$dir" || true)"
    if [[ -n "$hits" ]]; then
        echo "check-no-vital: $dir: Vital identifiers ($(echo "$hits" | wc -l | tr -d ' ') lines):" >&2
        echo "$hits" | head -n 5 | sed 's/^/    /' >&2
        found=1
    fi
    hits="$(grep -rniE 'GNU (Lesser |Library )?General Public License|(^|[^A])L?GPL-?[0-9]|(^|[^A])GPL' "$dir" | grep -v 'Affero' || true)"
    if [[ -n "$hits" ]]; then
        echo "check-no-vital: $dir: GPL header or license text ($(echo "$hits" | wc -l | tr -d ' ') lines):" >&2
        echo "$hits" | head -n 5 | sed 's/^/    /' >&2
        found=1
    fi
    return $found
}

self_test()
{
    local cxx="${CXX:-c++}" dir status=0
    dir="$(mktemp -d)"
    trap 'rm -rf "$dir"' RETURN

    cat > "$dir/clean.cpp" <<'CPP'
namespace synthyboy_selftest { int clean_marker() { return 1; } }
CPP
    cat > "$dir/tainted.cpp" <<'CPP'
namespace sbvital { int tainted_marker() { return 2; } }
CPP
    cat > "$dir/importer.cpp" <<'CPP'
namespace synthyboy { namespace vitalimport { int importer_marker() { return 3; } } }
int importVitalPreset() { return 4; }
CPP
    "$cxx" -c "$dir/clean.cpp" -o "$dir/clean.o"
    "$cxx" -c "$dir/tainted.cpp" -o "$dir/tainted.o"
    "$cxx" -c "$dir/importer.cpp" -o "$dir/importer.o"
    "$AR" rcs "$dir/libimporter.a" "$dir/importer.o"
    "$AR" rcs "$dir/libclean.a" "$dir/clean.o"
    "$AR" rcs "$dir/libtainted.a" "$dir/tainted.o"

    if check_archive "$dir/libclean.a" 2>/dev/null; then
        echo "self-test: a clean archive passes: ok"
    else
        echo "self-test FAILED: the clean archive was rejected" >&2
        status=1
    fi

    if check_archive "$dir/libtainted.a" 2>/dev/null; then
        echo "self-test FAILED: an archive with a symbol in namespace sbvital was accepted" >&2
        status=1
    else
        echo "self-test: an archive with a symbol in namespace sbvital is rejected: ok"
    fi

    if check_archive "$dir/libimporter.a" 2>/dev/null; then
        echo "self-test: the importer of the Vital file format (vitalimport, importVital*) passes: ok"
    else
        echo "self-test FAILED: the importer identifiers were rejected" >&2
        status=1
    fi

    # the source tree of the own engine: a Vital identifier or a GPL header fails, plain words and the AGPL header pass
    mkdir -p "$dir/src-clean" "$dir/src-vital" "$dir/src-gpl"
    printf '%s\n' '// SPDX-License-Identifier: AGPL-3.0-or-later' '// tuned against the reference engine, Vital by name only' 'int ok() { return 1; }' > "$dir/src-clean/a.cpp"
    printf '%s\n' '// SPDX-License-Identifier: AGPL-3.0-or-later' '#include "vital/synth_base.h"' 'int bad() { return sbvital::x(); }' > "$dir/src-vital/a.cpp"
    printf '%s\n' '// This program is free software under the GNU General Public License, version 3' 'int gpl() { return 2; }' > "$dir/src-gpl/a.cpp"
    if check_source "$dir/src-clean" 2>/dev/null; then
        echo "self-test: a source tree without Vital identifiers and with the AGPL header passes: ok"
    else
        echo "self-test FAILED: the clean source tree was rejected" >&2
        status=1
    fi
    if check_source "$dir/src-vital" 2>/dev/null; then
        echo "self-test FAILED: a source tree with a Vital identifier was accepted" >&2
        status=1
    else
        echo "self-test: a source tree with a Vital identifier is rejected: ok"
    fi
    if check_source "$dir/src-gpl" 2>/dev/null; then
        echo "self-test FAILED: a source tree with a GPL header was accepted" >&2
        status=1
    else
        echo "self-test: a source tree with a GPL header is rejected: ok"
    fi
    return $status
}

if [[ $# -eq 0 ]]; then
    echo "usage: $0 <archive.a>... | --self-test" >&2
    exit 2
fi

if [[ "$1" == "--self-test" ]]; then
    self_test
    exit $?
fi

if [[ "$1" == "--source" ]]; then
    shift
    [[ $# -gt 0 ]] || { echo "usage: $0 --source <dir>..." >&2; exit 2; }
    status=0
    for tree in "$@"; do
        if check_source "$tree"; then
            echo "check-no-vital: $tree: clean"
        else
            status=1
        fi
    done
    exit $status
fi

status=0
for archive in "$@"; do
    if check_archive "$archive"; then
        echo "check-no-vital: $archive: clean"
    else
        status=1
    fi
done
exit $status
