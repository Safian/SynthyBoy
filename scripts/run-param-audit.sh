#!/usr/bin/env bash
# SynthyBoy - the parameter effect audit, one entry point (docs/ui-design/PARAM-AUDIT-REPORT.md, docs/QUALITY.md "Parameter effect audit").
#
# Does the sound at the output really change, and the way it should, when a value changes? This script builds the tool if needed, runs
#   stage 1  (tools/param-audit)  every key of the parameter table through its context, at every value (--full) or a representative subset (--quick),
#            plus a sweep through a routed modulation source for every modulatable key, with per value assertions (finite, peak, click, edge, default twice);
#   stage 2  (synthyboy_test_param_semantics)  the families against their expected curves (pitch in cents, level in dB, pan law, filter, envelopes, LFO, mod matrix, effects ...),
# merges the shards, compares them with the committed baseline and writes the report.
#
# Usage: scripts/run-param-audit.sh [options]
#   --quick              representative values per key (9 per key, 5 for the modulation sweep); of a family (osc2/3, lfo, rlfo, env, mod, fxi) instance 1 and the last instance: minutes (CI)
#   --full               every value of every key (default). Instances 2 .. N of a family (osc2/3, lfo2..10, env2..6, rlfo2..4, mod02..64, fxi02..16) run at the quick density,
#                        because they are the same code as instance 1 and the report compares their verdicts with instance 1 (--all-instances sweeps them fully too: hours)
#   --all-instances      with --full: every instance at full density
#   --keys REGEX         only the keys matching REGEX (extended regex, searched in the key)
#   --group PREFIX       only the keys starting with PREFIX (filter1.  chorus.  osc2.  mod01.)
#   --part I/N           only every N-th key (the I-th share, 0 <= I < N): one of N machines of a CI matrix; the report then compares these keys with the baseline only
#   --jobs N             parallel processes by key shard (default: number of CPUs); the result does not depend on N
#   --baseline FILE      compare with this baseline (default docs/ui-design/param-audit-baseline.json); regressions = a changed verdict or curve
#   --update-baseline    write the baseline from this run (intended changes show up in git diff)
#   --engine vital|own   engine under test (default vital; "own" only for the keys it claims)
#   --seed N             seed handed to the tool (the renders are deterministic; recorded in the report)
#   --keep-wavs DIR      write the rendered audio of the swept keys as WAV files (listen to a key: --keys '^filter1\.cutoff$' --keep-wavs /tmp/w)
#   --list-keys          print the keys the run would cover with their support in the engine, and exit
#   --build-dir DIR      CMake build directory (default build; configured without the plug-in when it does not exist)
#   --out DIR            output directory (default <build-dir>/param-audit/<UTC timestamp>/ and a symlink latest)
#   --no-build           do not run cmake (ctest uses this: the tools are already built)
#   --no-semantics       skip stage 2
#   --semantics-only     only stage 2
#   --self-test          mutation test: injects 7 known faults (SYNTHYBOY_AUDIT_FAULT) into the harness and expects the audit to catch each; exits 1 if one slips through
#   --help
# Output (in the output directory): report.md, keys.csv, keys-compact.csv, keys.json (with the metric curves), summary.json, semantics.log, shard logs.
# Exit codes: 0 clean; 1 regression against the baseline, unexplained DEAD key, unexplained finding or failed semantic check (self-test: a fault was not caught);
#             2 usage error; 3 build problem (configure / build failed or the Vital engine is not part of the build).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

mode=full
keys=""
group=""
jobs=""
part=""
baseline="$root/docs/ui-design/param-audit-baseline.json"
update_baseline=0
engine=vital
seed=1
wavs=""
list_keys=0
build_dir="build"
out=""
semantics=1
stage1=1
no_build=0
self_test=0
all_instances=0

usage() { sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'; }
die_usage() { echo "run-param-audit.sh: $*" >&2; echo "try --help" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --quick) mode=quick ;;
        --full) mode=full ;;
        --all-instances) all_instances=1 ;;
        --keys) [ $# -ge 2 ] || die_usage "--keys needs a value"; keys="$2"; shift ;;
        --group) [ $# -ge 2 ] || die_usage "--group needs a value"; group="$2"; shift ;;
        --part) [ $# -ge 2 ] || die_usage "--part needs a value"; part="$2"; shift ;;
        --jobs) [ $# -ge 2 ] || die_usage "--jobs needs a value"; jobs="$2"; shift ;;
        --baseline) [ $# -ge 2 ] || die_usage "--baseline needs a value"; baseline="$2"; shift ;;
        --update-baseline) update_baseline=1 ;;
        --engine) [ $# -ge 2 ] || die_usage "--engine needs a value"; engine="$2"; shift ;;
        --seed) [ $# -ge 2 ] || die_usage "--seed needs a value"; seed="$2"; shift ;;
        --keep-wavs) [ $# -ge 2 ] || die_usage "--keep-wavs needs a value"; wavs="$2"; shift ;;
        --list-keys) list_keys=1 ;;
        --build-dir) [ $# -ge 2 ] || die_usage "--build-dir needs a value"; build_dir="$2"; shift ;;
        --out) [ $# -ge 2 ] || die_usage "--out needs a value"; out="$2"; shift ;;
        --no-build) no_build=1 ;;
        --no-semantics) semantics=0 ;;
        --semantics-only) stage1=0 ;;
        --self-test) self_test=1 ;;
        --help|-h) usage; exit 0 ;;
        *) die_usage "unknown option $1" ;;
    esac
    shift
done
case "$engine" in vital|own) ;; *) die_usage "--engine must be vital or own" ;; esac
case "$build_dir" in /*) ;; *) build_dir="$root/$build_dir" ;; esac
if [ -z "$jobs" ]; then
    jobs="$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) | head -n1)"
fi
case "$jobs" in ''|*[!0-9]*) die_usage "--jobs must be a number" ;; esac
[ "$jobs" -ge 1 ] || die_usage "--jobs must be at least 1"

# ---- build --------------------------------------------------------------------------------------------------------------------------------------------------
build() {
    if [ ! -f "$build_dir/CMakeCache.txt" ]; then
        echo "configuring $build_dir (core only: no plug-in, no bitmap GUI)"
        cmake -S "$root" -B "$build_dir" -DSYNTHYBOY_BUILD_PLUGIN=OFF -DSYNTHYBOY_BITMAP_GUI=OFF -DSYNTHYBOY_WITH_VITAL=ON >"$build_dir.configure.log" 2>&1 \
            || { echo "configure failed, see $build_dir.configure.log" >&2; exit 3; }
    fi
    if ! grep -q '^SYNTHYBOY_WITH_VITAL:BOOL=ON' "$build_dir/CMakeCache.txt"; then
        echo "the build directory $build_dir has no Vital engine (SYNTHYBOY_WITH_VITAL=OFF): the audit needs it" >&2
        exit 3
    fi
    cmake --build "$build_dir" --target synthyboy_param_audit synthyboy_test_param_semantics -j "$jobs" >"$build_dir/param-audit-build.log" 2>&1 \
        || { echo "build failed, see $build_dir/param-audit-build.log" >&2; tail -20 "$build_dir/param-audit-build.log" >&2; exit 3; }
}
find_tool() {
    for c in "$build_dir/tools/param-audit/$1" "$build_dir/tools/param-audit/Release/$1"; do
        [ -x "$c" ] && { echo "$c"; return; }
    done
    echo "$1 not found in $build_dir" >&2
    exit 3
}
[ "$no_build" -eq 1 ] || build
audit="$(find_tool synthyboy_param_audit)"
semtest="$(find_tool synthyboy_test_param_semantics)"

if [ "$list_keys" -eq 1 ]; then
    args=(--engine "$engine")
    [ -n "$keys" ] && args+=(--keys "$keys")
    [ -n "$group" ] && args+=(--group "$group")
    exec "$audit" --list-keys "${args[@]}"
fi

# ---- self test ----------------------------------------------------------------------------------------------------------------------------------------------
if [ "$self_test" -eq 1 ]; then
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    missed=0
    expect_caught() { # name, how, command...
        local name="$1" how="$2"
        shift 2
        if SYNTHYBOY_AUDIT_FAULT="$name" "$@" >"$tmp/$name.log" 2>&1; then
            echo "  fault $name ($how): NOT CAUGHT"
            missed=$((missed + 1))
        else
            echo "  fault $name ($how): caught"
        fi
    }
    echo "self test: a clean run must pass first"
    "$semtest" filter_resonance >"$tmp/clean.log" 2>&1 || { echo "the clean semantic test fails, the self test is meaningless"; cat "$tmp/clean.log"; exit 1; }
    echo "self test: injecting faults"
    expect_caught cutoff_scaled "cutoff scaled by a factor 1.5" "$semtest" filter_resonance
    expect_caught pan_swapped "left and right swapped" "$semtest" level_pan
    expect_caught delay_time_2x "delay line twice as long as displayed" "$semtest" delay_time
    expect_caught tune_cents "oscillator tune off by 2 cents" "$semtest" pitch_tune_cents
    expect_caught volume_db_as_linear "master volume at half the dB" "$semtest" level_master_volume
    expect_caught env_attack_half "attack time halved" "$semtest" envelope_times
    # an effect left unwired: stage 1 must find the chorus controls DEAD
    ( SYNTHYBOY_AUDIT_FAULT=chorus_unwired "$audit" --quick --keys '^chorus\.(depth|rate|feedback|delay1)$' --out "$tmp/chorus" >/dev/null 2>&1
      SYNTHYBOY_AUDIT_FAULT=chorus_unwired python3 -I "$here/param_audit_report.py" --in "$tmp/chorus" --out "$tmp/chorus-report" --mode quick --keys-regex x >"$tmp/chorus.log" 2>&1 ) \
        && { echo "  fault chorus_unwired (effect controls not reaching the engine): NOT CAUGHT"; missed=$((missed + 1)); } \
        || echo "  fault chorus_unwired (effect controls not reaching the engine): caught"
    if [ "$missed" -ne 0 ]; then
        echo "self test FAILED: $missed fault(s) slipped through"
        exit 1
    fi
    echo "self test passed: all 7 injected faults were caught"
    exit 0
fi

# ---- run ----------------------------------------------------------------------------------------------------------------------------------------------------
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
[ -n "$out" ] || out="$build_dir/param-audit/$stamp"
mkdir -p "$out"
case "$out" in
    "$build_dir/param-audit/"*) ln -sfn "$out" "$build_dir/param-audit/latest" ;;
esac
start="$(date +%s)"
echo "param-audit: mode $mode, engine $engine, $jobs job(s), output $out"

if [ "$stage1" -eq 1 ]; then
    shard_args=(--engine "$engine" --seed "$seed" --out "$out")
    if [ "$mode" = quick ]; then shard_args+=(--quick); else shard_args+=(--full); fi
    [ "$all_instances" -eq 1 ] && shard_args+=(--all-instances)
    [ -n "$keys" ] && shard_args+=(--keys "$keys")
    [ -n "$group" ] && shard_args+=(--group "$group")
    [ -n "$part" ] && shard_args+=(--part "$part")
    [ -n "$wavs" ] && shard_args+=(--keep-wavs "$wavs")
    # quick: the shards take the keys from a shared queue (a claim directory per key) instead of a fixed share, so that they finish together; the result of a key does not depend on the shard
    claims=""
    if [ "$mode" = quick ] && [ "$jobs" -gt 1 ]; then
        claims="$out/claims"
        rm -rf "$claims"
    fi
    pids=""
    i=0
    while [ "$i" -lt "$jobs" ]; do
        "$audit" "${shard_args[@]}" --shard "$i/$jobs" ${claims:+--claims "$claims"} >"$out/shard-$i.log" 2>&1 &
        pids="$pids $!"
        i=$((i + 1))
    done
    failed=0
    for p in $pids; do
        wait "$p" || failed=1
    done
    if [ "$failed" -ne 0 ]; then
        echo "a shard of the audit tool failed:" >&2
        tail -5 "$out"/shard-*.log >&2
        exit 3
    fi
fi

sem_log=""
sem_rc=0
if [ "$semantics" -eq 1 ]; then
    sem_log="$out/semantics.log"
    "$semtest" >"$sem_log" 2>&1 || sem_rc=$?
    tail -1 "$sem_log"
fi

end="$(date +%s)"
if [ "$stage1" -eq 1 ]; then
    rep_args=(--in "$out" --out "$out" --mode "$mode" --engine "$engine" --wall "$((end - start))" --exemptions "$root/tools/param-audit/exemptions.tsv")
    [ -n "$sem_log" ] && rep_args+=(--semantics-log "$sem_log")
    if [ -z "$keys$group" ]; then
        [ -f "$baseline" ] && rep_args+=(--baseline "$baseline")
    else
        rep_args+=(--keys-regex "${keys:-$group}")
        [ -f "$baseline" ] && rep_args+=(--baseline "$baseline")
    fi
    [ "$update_baseline" -eq 1 ] && rep_args+=(--update-baseline --baseline "$baseline")
    rc=0
    python3 -I "$here/param_audit_report.py" "${rep_args[@]}" || rc=$?
    echo "wall time $((end - start)) s; report: $out/report.md"
    [ "$rc" -ne 0 ] && exit 1
fi
[ "$sem_rc" -ne 0 ] && { echo "semantic checks failed, see $sem_log" >&2; exit 1; }
exit 0
