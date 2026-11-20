#!/usr/bin/env bash
# SynthyBoy - the reference comparison against the real Serum 2, one entry point (docs/ui-design/REFCOMPARE-REPORT.md).
#
# Renders the real Serum 2 presets (tests/serum-presets/) through the installed Serum 2 plug-in (tools/serum-reference, macOS only, the plug-in is licensed software and is
# never touched beyond loading it: a dialog or an authorization stops that preset and is recorded), imports them with our importer, renders the imported presets through the
# Vital-derived engine with the same note script (tools/ref-compare/ref_render.cpp) and compares the pairs (tools/ref-compare/ref_compare.py: envelope, log-mel and multi-resolution
# STFT distance, centroid / rolloff, f0 in cents, stereo, LUFS / peak, noise floor, tail) into a 0..100 score and a class (MATCH / CLOSE / DIFFERENT / FAILED).
#
# Usage: scripts/run-ref-compare.sh [options]
#   --render-only        only the renders (Serum reference where missing, ours always), no comparison
#   --compare-only       only compare the renders that exist (no build, no import, no render)
#   --preset REGEX       only the presets whose path below Presets/ matches REGEX (extended regex, searched in "Factory/Bass/Name.SerumPreset")
#   --calibrate          the calibration of the score: 5 crafted presets x 4 deliberately broken renders of ours (50 cents sharp, oscillator off, cutoff three octaves down, 6 dB down) must score at least 10 points lower and not MATCH; exit 1 if not
#   --catalog [GROUP]    score single module presets (filters, warps, fx, unison, env, osc; default all) against the real Serum 2: the worst modules in one table
#   --list               print the renders the run would cover (id, note script, path) and exit
#   --jobs N             parallel processes of our render and of the comparison (default 4; the Serum reference render is always one process at low priority)
#   --scripts LIST       note scripts: c3 (C3 vel 100, all presets), c1 (C1 vel 64, basses), c5 (C5 vel 127, leads / plucks / chords...); default c3,c1,c5
#   --baseline FILE      compare with this baseline (default docs/ui-design/refcompare-baseline.json): a class that got worse or a score that dropped by more than 2.0 is a regression
#   --update-baseline    write the baseline from this run (the change shows up in git diff)
#   --presets-dir DIR    the Serum 2 presets (default tests/serum-presets/Presets)
#   --ref-dir DIR        the reference renders (default tests/serum-reference/wav; gitignored: licensed plug-in output, never commit it)
#   --work-dir DIR       imported presets, our renders (default tests/serum-reference)
#   --out DIR            report directory (default tests/serum-reference/report/<UTC timestamp>/ and a symlink report/latest)
#   --ref2-dir DIR       a second reference render of the same presets: per preset noise floor of the metrics (two Serum renders of the same preset)
#   --build-dir DIR      CMake build directory (default build)
#   --no-build           do not run cmake
#   --retry-failed       render the reference again for the presets that failed before
#   --timeout SEC        timeout of one Serum render (default 90)
#   --help
# Resumable: the reference render skips what has a sidecar with the same preset hash; every step can be run again.
# Output (in the report directory): report.md, results.csv, results.json, summary.json, ab/ (reference / ours listening files of the worst 30 and 20 random renders).
# Exit codes: 0 clean; 1 regression against the baseline; 2 usage error; 3 build / tool problem.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

mode=all
regex=""
list=0
calibrate=0
catalog=""
jobs=4
scripts="c3,c1,c5"
baseline="$root/docs/ui-design/refcompare-baseline.json"
update=0
presets="$root/tests/serum-presets/Presets"
refdir="$root/tests/serum-reference/wav"
work="$root/tests/serum-reference"
out=""
ref2=""
build="$root/build"
dobuild=1
retry=""
timeout=90

usage() { sed -n '2,/^set -euo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        --render-only) mode=render ;;
        --compare-only) mode=compare; dobuild=0 ;;
        --preset) regex="${2:?}"; shift ;;
        --list) list=1 ;;
        --calibrate) calibrate=1 ;;
        --catalog) catalog="all"; if [ $# -gt 1 ] && [[ "$2" != --* ]]; then catalog="$2"; shift; fi ;;
        --jobs) jobs="${2:?}"; shift ;;
        --scripts) scripts="${2:?}"; shift ;;
        --baseline) baseline="${2:?}"; shift ;;
        --update-baseline) update=1 ;;
        --presets-dir) presets="${2:?}"; shift ;;
        --ref-dir) refdir="${2:?}"; shift ;;
        --work-dir) work="${2:?}"; shift ;;
        --out) out="${2:?}"; shift ;;
        --ref2-dir) ref2="${2:?}"; shift ;;
        --build-dir) build="${2:?}"; shift ;;
        --no-build) dobuild=0 ;;
        --retry-failed) retry="--retry-failed" ;;
        --timeout) timeout="${2:?}"; shift ;;
        --help|-h) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

tools="$root/tools"
render_ref="$build/tools/serum-reference/synthyboy_serum_reference"
render_ours="$build/tools/ref-compare/synthyboy_ref_render"
importer="$build/tools/serum-import/synthyboy_serum_import"
imported="$work/imported"
ours="$work/ours"

if [ ! -d "$presets" ]; then echo "no presets at $presets (tests/serum-presets is not part of the repository)" >&2; exit 2; fi
command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 3; }
python3 -c "import numpy, scipy" 2>/dev/null || { echo "python3 needs numpy and scipy" >&2; exit 3; }

if [ "$calibrate" = 1 ] || [ -n "$catalog" ]; then
    [ "$(uname)" = Darwin ] && [ -x "$render_ref" ] || { echo "the catalog and the calibration render the real Serum 2: macOS and $render_ref are required" >&2; exit 3; }
    if [ "$dobuild" = 1 ]; then cmake --build "$build" --target synthyboy_ref_render synthyboy_serum_import synthyboy_serum_reference >/dev/null || exit 3; fi
    if [ "$calibrate" = 1 ]; then exec python3 "$tools/ref-compare/catalog.py" --root "$root" --build "$build" --group calib; fi
    exec python3 "$tools/ref-compare/catalog.py" --root "$root" --build "$build" --group "$catalog"
fi

if [ "$list" = 1 ]; then
    exec python3 -I "$tools/serum-reference/render_all.py" --host "$render_ref" --presets "$presets" --out "$refdir" --filter "$regex" --scripts "$scripts" --list
fi

if [ "$dobuild" = 1 ]; then
    if [ ! -d "$build" ]; then
        cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null || { echo "configure failed" >&2; exit 3; }
    fi
    targets="synthyboy_ref_render synthyboy_serum_import"
    if [ "$(uname)" = Darwin ] && grep -q synthyboy_serum_reference "$build/build.ninja" 2>/dev/null; then targets="$targets synthyboy_serum_reference"; fi
    # shellcheck disable=SC2086
    cmake --build "$build" --target $targets >/dev/null || { echo "build failed (cmake --build $build --target $targets)" >&2; exit 3; }
fi

if [ "$mode" != compare ]; then
    # 1. the Serum 2 reference (macOS, one process, low priority; resumable)
    if [ -x "$render_ref" ]; then
        python3 -I "$tools/serum-reference/render_all.py" --host "$render_ref" --presets "$presets" --out "$refdir" --filter "$regex" --scripts "$scripts" --timeout "$timeout" $retry \
            || echo "reference render finished with failures (recorded as .fail.json)" >&2
    else
        echo "no reference renderer ($render_ref): the existing reference renders in $refdir are used" >&2
    fi
    # 2. import
    mkdir -p "$imported"
    if [ -z "$regex" ]; then
        "$importer" --in "$presets" --out "$imported" --only SerumPreset --content-dir "$imported/content" --summary "$work/import-summary.json" >/dev/null || echo "importer reported failures" >&2
    else
        python3 -I "$tools/serum-reference/render_all.py" --host x --presets "$presets" --out x --filter "$regex" --scripts c3 --list | cut -f3 | while IFS= read -r rel; do
            sub="$(dirname "$rel")"
            "$importer" --in "$presets/$rel" --out "$imported/$sub" --serum-root "$(dirname "$presets")" --content-dir "$imported/content" >/dev/null || echo "importer failed on $rel" >&2
        done
    fi
    # 3. ours
    rm -rf "$ours"
    python3 "$tools/ref-compare/ref_compare.py" render-ours --presets "$presets" --ref "$refdir" --imported "$imported" --ours "$ours" --render-bin "$render_ours" \
        --filter "$regex" --scripts "$scripts" --jobs "$jobs"
fi
[ "$mode" = render ] && exit 0

# 4. compare + report
[ -n "$out" ] || out="$work/report/$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$out"
ref2args=()
[ -n "$ref2" ] && ref2args=(--ref2 "$ref2")
python3 "$tools/ref-compare/ref_compare.py" compare --presets "$presets" --ref "$refdir" --ours "$ours" --out "$out" --filter "$regex" --scripts "$scripts" --jobs "$jobs" ${ref2args[@]+"${ref2args[@]}"}
ln -sfn "$out" "$(dirname "$out")/latest" 2>/dev/null || true
rc=0
upd=()
[ "$update" = 1 ] && upd=(--update-baseline)
python3 "$tools/ref-compare/report.py" --results "$out/results.json" --ref "$refdir" --ours "$ours" --out "$out" --baseline "$baseline" ${upd[@]+"${upd[@]}"} || rc=$?
exit "$rc"
