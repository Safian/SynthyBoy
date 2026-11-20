#!/usr/bin/env bash
# SynthyBoy - the runner of the device measurements of docs/plans/perf-measurement.md (section 2): installs the benchmarks on an MPC, reads the read-only probes, runs
# the interference, noise floor, PMU, module table and libm steps, and fetches the CSV files. Every remote and local command is printed with a leading "+ ".
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/perf-device.sh --device k61|xl [--host IP] --phase probe|install|e0|e1|e2|ab|pmu|modules|libm|cleanup
#                               [--rt N] [--bin static|dyn|sections] [--seconds 10] [--loads set12|set6:<name>,<name>,...] [--sequence ABBA...]
#                               [--out DIR] [--dry-run] [--i-know-the-host-stops] [--no-host-stop]
#
#   --device    k61 (MPC Key 61: root@192.168.0.30, core 3, profile low) or xl (MPC XL: root@192.168.0.126, core 6, profile high,voices=16,control=economy)
#   --host      another address of the device (DHCP)
#   --phase     probe    D0: the read-only probe (cores, host audio threads and their priority, rt throttling, PMU, kernel config, caches, thermal, libm)
#               install  the static / dyn / sections builds, the factory presets and the sample pack into /tmp/sbperf on the device (tar over ssh)
#               e0       today's bench, 5 passes, and a snapshot of the threads of the bench process
#               e1       interference: --core only; --rt P; --rt P+1; and the same with the host stopped (needs --i-know-the-host-stops, never on the xl;
#                        --no-host-stop runs the first three settings only)
#               e2       the noise floor: the same binary twice (A and a copy B), the ABBA loop, perf-ab.py
#               ab       the ABBA loop of the static build (A) against --bin (B, default dyn), perf-ab.py
#               pmu      synthyboy_pmu_probe, then tier_bench --pmu cache and --pmu compute
#               modules  tier_bench --modules, and the table (perf-tables.py modules)
#               libm     synthyboy_libm_probe static / dyn alternating 7 times, then the A/B of static against dyn on three loads (E5)
#               cleanup  rm -rf /tmp/sbperf
#   --rt N      the real-time priority of the runs (default 20; e1: the host's priority HP, read in the probe)
#   --bin       the build the phase runs (default static); sections only for a phase that prints sections
#   --seconds   measured audio per run (integer, default 10)
#   --loads     set12 (default) or set6:<the four preset names> (SET6 = basic, heavy and these)
#   --sequence  the ABBA... sequence of ab and e2 (default ABBA five times = 10 pairs)
#   --out       where the results go (default build-tier-bench/results/<device>-<UTC time>)
#   --dry-run   print every command prefixed with "+ " and run nothing (no ssh, no file is made)
#
# The build is made by scripts/build-tier-bench.sh --arch arm32|arm64 --dynamic --sections; PERF_BUILD_OUT names its output directory (default build-tier-bench/out).

set -euo pipefail

me="perf-device.sh"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
device=""
host=""
phase=""
rt=20
bin=""
seconds=10
loads_arg="set12"
sequence="ABBAABBAABBAABBAABBA"
out_dir=""
dry_run=0
host_stop_ok=0
no_host_stop=0
build_out="${PERF_BUILD_OUT:-build-tier-bench/out}"

die() { echo "$me: $*" >&2; exit 2; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --device) device="${2:-}"; shift 2 ;;
        --host) host="${2:-}"; shift 2 ;;
        --phase) phase="${2:-}"; shift 2 ;;
        --rt) rt="${2:-}"; shift 2 ;;
        --bin) bin="${2:-}"; shift 2 ;;
        --seconds) seconds="${2:-}"; shift 2 ;;
        --loads) loads_arg="${2:-}"; shift 2 ;;
        --sequence) sequence="${2:-}"; shift 2 ;;
        --out) out_dir="${2:-}"; shift 2 ;;
        --dry-run) dry_run=1; shift ;;
        --i-know-the-host-stops) host_stop_ok=1; shift ;;
        --no-host-stop) no_host_stop=1; shift ;;
        -h|--help) sed -n '7,32p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option $1 (see --help)" ;;
    esac
done

case "$device" in
    k61) default_host="192.168.0.30"; core=3; profile="low"; arch="arm32"; modules_profile="low,fx=all" ;;
    xl)  default_host="192.168.0.126"; core=6; profile="high,voices=16,control=economy"; arch="arm64"; modules_profile="high,voices=16,control=economy" ;;
    *) die "--device must be k61 or xl" ;;
esac
case "$phase" in
    probe|install|e0|e1|e2|ab|pmu|modules|libm|cleanup) ;;
    *) die "--phase must be one of probe install e0 e1 e2 ab pmu modules libm cleanup" ;;
esac
[[ "$rt" =~ ^[0-9]+$ ]] && (( rt >= 1 && rt <= 98 )) || die "--rt must be 1 .. 98 (e1 also runs at rt + 1)"
[[ "$seconds" =~ ^[0-9]+$ ]] && (( seconds >= 1 )) || die "--seconds must be a positive integer"
[[ "$sequence" =~ ^[AB]+$ ]] && (( ${#sequence} % 2 == 0 )) || die "--sequence must be A and B only, of even length (ABBA...)"
[[ -z "$bin" || "$bin" == static || "$bin" == dyn || "$bin" == sections ]] || die "--bin must be static, dyn or sections"
if [[ "$phase" == e1 && "$no_host_stop" == 0 ]]; then
    [[ "$device" == xl ]] && die "e1 stops the host in its last setting: never on the xl (use --no-host-stop for the first three settings)"
    [[ "$host_stop_ok" == 1 ]] || die "e1 stops the host (kill -STOP of acvs, with a dead-man kill -CONT): needs --i-know-the-host-stops (or --no-host-stop for the first three settings)"
fi
host="${host:-$default_host}"
H="root@$host"
SSH_OPTS="-o BatchMode=yes -o ConnectTimeout=5"
if [[ -z "$out_dir" ]]; then
    out_dir="build-tier-bench/results/$device-$(date -u +%Y%m%dT%H%M%SZ)"
fi
store="/tmp/sbperf/store"
pack="com.safian.synthyboy.factory-samples"

# ---- the loads ----
set12=("basic" "heavy" "worst" "Twin Warp Lead" "Hyper Saw Lead" "Diffusor Cloud Pad" "Scream Filter Bass" "Stretched Glass Choir" "Triple Fold Reese" "Ring Mod Gamelan" "Pentatonic Step Pluck" "Spectral Vowel Freeze")
case "$loads_arg" in
    set12) loads=("${set12[@]}") ;;
    set6:*)
        loads=("basic" "heavy")
        IFS=',' read -r -a named <<< "${loads_arg#set6:}"
        for n in "${named[@]}"; do [[ -n "$n" ]] && loads+=("$n"); done
        (( ${#loads[@]} > 2 )) || die "--loads set6: needs the preset names after the colon"
        ;;
    *) die "--loads must be set12 or set6:<name>,<name>,..." ;;
esac
load_args() {   # the --load "name" list of the loads given as arguments
    local out="" n
    for n in "$@"; do out+=" --load \"$n\""; done
    printf '%s' "${out# }"
}

# ---- running and printing commands ----
trace() { if (( dry_run )); then printf '+ %s\n' "$*"; else printf '+ %s\n' "$*" >&2; fi; }
remote() {   # remote 'command line' [output file]: a command line for the sh of the device
    local cmd="$1" file="${2:-}"
    if [[ -n "$file" ]]; then trace "ssh $SSH_OPTS $H '$cmd' > $file"; else trace "ssh $SSH_OPTS $H '$cmd'"; fi
    (( dry_run )) && return 0
    if [[ -n "$file" ]]; then ssh $SSH_OPTS "$H" "$cmd" > "$file"; else ssh $SSH_OPTS "$H" "$cmd"; fi
}
remote_script() {   # remote_script [output file] < script: the script text goes to "sh -s" of the device
    local file="${1:-}" text line
    text="$(cat)"
    trace "ssh $SSH_OPTS $H 'sh -s' <<'EOF'${file:+ > $file}"
    if (( dry_run )); then
        while IFS= read -r line; do printf '+   %s\n' "$line"; done <<< "$text"
        printf '+ EOF\n'
        return 0
    fi
    if [[ -n "$file" ]]; then ssh $SSH_OPTS "$H" 'sh -s' <<< "$text" > "$file"; else ssh $SSH_OPTS "$H" 'sh -s' <<< "$text"; fi
}
local_cmd() {   # a local command line (a pipeline allowed)
    trace "$*"
    (( dry_run )) || eval "$*"
}
fetch() { remote "cat /tmp/sbperf/$1" "$out_dir/$1"; }
prepare_out() { local_cmd "mkdir -p $out_dir"; }
tier() {   # tier <binary dir> <arguments...>: the command line of one tier_bench run on the device
    local d="$1"; shift
    printf 'cd /tmp/sbperf && SYNTHYBOY_CONTENT_DIR=%s ./%s/synthyboy_tier_bench --presets presets %s' "$store" "$d" "$*"
}

# ---- phases ----
phase_probe() {
    remote_script <<'EOF'
echo "== kernel";  uname -a; cat /proc/device-tree/model; echo
echo "== cpus";    nproc; echo "online $(cat /sys/devices/system/cpu/online) isolated $(cat /sys/devices/system/cpu/isolated)"; cat /proc/cmdline
for c in /sys/devices/system/cpu/cpu[0-9]*; do echo "$(basename $c) gov=$(cat $c/cpufreq/scaling_governor 2>&1) cur=$(cat $c/cpufreq/scaling_cur_freq 2>&1) max=$(cat $c/cpufreq/scaling_max_freq 2>&1)"; done
echo "== caches cpu3"; for i in /sys/devices/system/cpu/cpu3/cache/index*; do echo "$(basename $i) L$(cat $i/level) $(cat $i/type) $(cat $i/size)"; done 2>&1
echo "== thermal"; for z in /sys/class/thermal/thermal_zone*; do echo "$(basename $z) $(cat $z/type) $(cat $z/temp)"; done 2>&1
echo "== rt throttle"; echo "runtime_us $(cat /proc/sys/kernel/sched_rt_runtime_us) period_us $(cat /proc/sys/kernel/sched_rt_period_us)"
echo "== pmu"; echo "paranoid $(cat /proc/sys/kernel/perf_event_paranoid)"; ls /sys/bus/event_source/devices 2>&1; dmesg | grep -i -E "perfevents|pmu" | head -5
echo "== kernel config"; (zcat /proc/config.gz 2>/dev/null | grep -E '^CONFIG_(PERF_EVENTS|HW_PERF_EVENTS|SCHEDSTATS|SCHED_INFO|PREEMPT|HZ=|NO_HZ|ARM_ARCH_TIMER|VDSO)') || echo "no /proc/config.gz"
echo "== host audio threads"
P=$(systemctl show -p MainPID acvs | cut -d= -f2); echo "acvs MainPID=$P"
for t in /proc/$P/task/*; do echo "$(basename $t) $(cat $t/comm) $(sed 's/^.*) //' $t/stat | awk '{print "cpu="$37" rtprio="$38" policy="$39}') $(grep Cpus_allowed_list $t/status | tr -d '\t')"; done
echo "== libm the host process uses"; grep -o '/[^ ]*libm[^ ]*' /proc/$P/maps | sort -u; ls -l /lib/libm.so.6 /lib/*/libm.so.6 2>&1
EOF
}

phase_install() {
    remote "mkdir -p /tmp/sbperf/static /tmp/sbperf/dyn /tmp/sbperf/sections $store/$pack"
    local v d
    for v in "" -dyn -sections; do
        if [[ -z "$v" ]]; then d=static; else d="${v#-}"; fi
        local_cmd "COPYFILE_DISABLE=1 tar -C $build_out/tier-bench-$arch$v -cf - . | ssh $SSH_OPTS $H \"tar -C /tmp/sbperf/$d -xf -\""
    done
    local_cmd "COPYFILE_DISABLE=1 tar -C content -cf - presets | ssh $SSH_OPTS $H 'tar -C /tmp/sbperf -xf -'"
    local_cmd "COPYFILE_DISABLE=1 tar -C content/factory-samples -cf - . | ssh $SSH_OPTS $H 'tar -C $store/$pack -xf -'"
}

phase_e0() {
    local b="${bin:-static}" two=(basic "Diffusor Cloud Pad")
    prepare_out
    local cmd
    cmd="$(tier "$b" "--core $core --config \"$profile\" $(load_args "${two[@]}") --seconds $seconds --repeat 5 --csv e0.csv")"
    trace "ssh $SSH_OPTS $H '$cmd' > $out_dir/e0.txt &"
    if (( ! dry_run )); then ssh $SSH_OPTS "$H" "$cmd" > "$out_dir/e0.txt" & fi
    local_cmd "sleep 20"
    remote_script "$out_dir/threads.txt" <<'EOF'
B=$(pidof synthyboy_tier_bench | cut -d' ' -f1); echo "bench pid $B"
for t in /proc/$B/task/*; do echo "$(basename $t) $(cat $t/comm) schedstat[run_ns wait_ns slices]=$(cat $t/schedstat 2>&1) $(sed 's/^.*) //' $t/stat | awk '{print "cpu="$37" rtprio="$38" policy="$39" nice="$17}') $(grep Cpus_allowed_list $t/status | tr -d '\t')"; done
EOF
    (( dry_run )) || wait
    fetch e0.csv
}

phase_e1() {
    local b="${bin:-static}" two=(basic "Diffusor Cloud Pad") hp="$rt" hp1=$((rt + 1)) n=0 cmd
    prepare_out
    local settings=("" "--rt $hp --rt-require" "--rt $hp1 --rt-require")
    local s
    for s in "${settings[@]}"; do
        n=$((n + 1))
        cmd="$(tier "$b" "--core $core ${s:+$s }--config \"$profile\" $(load_args "${two[@]}") --seconds $seconds --repeat 5 --csv e1-$n.csv")"
        remote "$cmd" "$out_dir/e1-$n.txt"
        fetch "e1-$n.csv"
    done
    (( no_host_stop )) && return 0
    local dead=$(( 2 * 5 * (seconds + 1) + 40 ))
    cmd="$(tier "$b" "--core $core --rt $hp --rt-require --config \"$profile\" $(load_args "${two[@]}") --seconds $seconds --repeat 5 --csv e1-4.csv")"
    # the host is stopped for the run only; the dead-man continues it after $dead s whatever happens to this script or the connection
    remote_script "$out_dir/e1-4.txt" <<EOF
P=\$(systemctl show -p MainPID acvs | cut -d= -f2); [ "\$P" -gt 1 ] || exit 1
( sleep $dead; kill -CONT \$P ) >/dev/null 2>&1 &
kill -STOP \$P
$cmd
kill -CONT \$P
EOF
    fetch e1-4.csv
}

ab_loop() {   # ab_loop <A binary dir/name> <B binary dir/name> <loads array name> <profile> <sequence> <label>
    local a="$1" bb="$2" ref="$3[@]" prof="$4" seq="$5" label="$6" i ch bin_path files=() cmd
    local items=("${!ref}")
    prepare_out
    for (( i = 0; i < ${#seq}; i++ )); do
        ch="${seq:i:1}"
        if [[ "$ch" == A ]]; then bin_path="$a"; else bin_path="$bb"; fi
        local csv="pass-$((i + 1))-$ch.csv"
        cmd="cd /tmp/sbperf && SYNTHYBOY_CONTENT_DIR=$store ./$bin_path --presets presets --core $core --rt $rt --rt-require --config \"$prof\" $(load_args "${items[@]}") --seconds $seconds --csv $csv"
        remote "$cmd" "$out_dir/pass-$((i + 1))-$ch.txt"
        fetch "$csv"
        files+=("$out_dir/$csv")
    done
    local_cmd "python3 -I $repo_root/scripts/perf-ab.py --sequence $seq --metric median_pct ${files[*]} > $out_dir/ab-$label.txt"
}

phase_e2() {
    local b="${bin:-static}"
    remote "cd /tmp/sbperf && cp $b/synthyboy_tier_bench $b/synthyboy_tier_bench_copy"
    ab_loop "$b/synthyboy_tier_bench" "$b/synthyboy_tier_bench_copy" loads "$profile" "$sequence" aa
}

phase_ab() {
    ab_loop "static/synthyboy_tier_bench" "${bin:-dyn}/synthyboy_tier_bench" loads "$profile" "$sequence" ab
}

phase_pmu() {
    local b="${bin:-static}" one=("Diffusor Cloud Pad") set cmd
    prepare_out
    remote "cd /tmp/sbperf && ./$b/synthyboy_pmu_probe --cpu $core" "$out_dir/pmu-probe.txt"
    for set in cache compute; do
        cmd="$(tier "$b" "--core $core --config \"$profile\" $(load_args "${one[@]}") --seconds $seconds --pmu $set --pmu-require --csv pmu-$set.csv")"
        remote "$cmd" "$out_dir/pmu-$set.txt"
        fetch "pmu-$set.csv"
    done
}

phase_modules() {
    local b="${bin:-static}" cmd
    prepare_out
    cmd="$(tier "$b" "--core $core --rt $rt --modules --seconds 2 --module-reps 3 --config \"$modules_profile\"")"
    remote "$cmd" "$out_dir/modules.txt"
    local_cmd "python3 -I $repo_root/scripts/perf-tables.py modules $out_dir/modules.txt > $out_dir/modules.md"
}

phase_libm() {
    local i
    prepare_out
    for i in 1 2 3 4 5 6 7; do
        remote "cd /tmp/sbperf && ./static/synthyboy_libm_probe" "$out_dir/libm-static-$i.txt"
        remote "cd /tmp/sbperf && ./dyn/synthyboy_libm_probe" "$out_dir/libm-dyn-$i.txt"
    done
    local e5=("worst" "Bode Barber Riser" "Hyper Saw Lead")
    ab_loop "static/synthyboy_tier_bench" "dyn/synthyboy_tier_bench" e5 "low,fx=all" "$sequence" libm
}

phase_cleanup() { remote "rm -rf /tmp/sbperf"; }

"phase_$phase"
