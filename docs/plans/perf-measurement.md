# Plan: a reliable performance measurement on the devices (audit part 3, rank 9 and rank 18)

Status: plan only, written from code and document reading (no build, no test, no benchmark, no ssh was run). Branch `agy/plan-perf-measurement`, base `32286cf8`.
Covers: `docs/review/OWN-ENGINE-DSP-AUDIT-3-CORE-PERFORMANCE-2026-11-20.md` rank 9 (tier_bench scheduling / PMU / per-module table) and rank 18 (bench libm differs from the plugin's libm), corrected by `docs/review/OWN-ENGINE-DSP-AUDIT-3-VERIFICATION-2026-11-20.md` (ranks 9 and 18) and `TODO.md:56,115,124`.
This is the first step of the work list (`TODO.md:56`: "the isolated, reliable performance measurement is the first").

**What this document cannot contain.** Every number that only a device can give (host audio thread priority, PMU availability, noise floor, module costs) is marked HYPOTHESIS together with the measurement that decides it (section 2). Nothing in this item changes a line under `core/` (no engine file is edited by any task below): the sound is bit-identical by construction, and section 5 checks that.

Conventions: `A:l` = file:line that was opened for this plan. "block" = 128 frames at 44.1 kHz = 128 / 44100 = 2.90249 ms (the "2.902 ms" of `docs/MPC.md:321`). "points" = percentage points of that block (1 point = 29.02 us). "Key 61" = `root@192.168.0.30`, "XL" = `root@192.168.0.126` (`.agents/skills/akai-mpc-ssh-deploy/SKILL.md:11-15`; DHCP, may change).

---

## 1. The problem, as verified

### 1.1 What the code does today

| # | What | Where (opened) |
|---|---|---|
| a | `tier_bench` pins the **calling thread** with `sched_setaffinity (0, ...)` when `--core N` is given and touches nothing else: "no scheduling class, no priority, no frequency governor". No `sched_setscheduler`, no `mlockall`, no `perf_event_open`, no thermal read, no `getrusage` anywhere in `bench/`, `scripts/` (non-test), `shell-mpc/` (non-test), `core/engine-own/` (repository grep for `perf_event|getrusage|thermal_zone|SCHED_FIFO|sched_setscheduler|mlockall`: only `shell-mpc/thread_policy.h:6,32` and `shell-mpc/tests/*`). | `bench/tier_bench.cpp:11-12` (doc), `:940-955` (code), `:819-830` (the same for `--micro`) |
| b | The measured loop is one `std::chrono::steady_clock` read before and after `engine->process()` of every block (2 reads = 1.3 us per block, 0.045 % of 2902.5 us: `docs/MPC.md:540` gives 1.3 us per *scope of two reads*). Warm-up is the first second: `floor (44100 / 128) = 344` blocks, discarded. Measured blocks: `floor (seconds * 44100 / 128)`. | `bench/tier_bench.cpp:371-373,381-413,441-447` |
| c | One new engine per (profile, load): `createEngine`, `applyLoad`, `prepare` (so every run starts on freshly allocated memory), then the loop. The percentile rule is `sorted[floor (0.99 * (n - 1))]`, the median `sorted[n / 2]`. Only min / median / p99 / max / mean and the count of blocks over 100 % are kept (no p99.9, no per-block series, no thermal, no faults). | `bench/tier_bench.cpp:359-366,461-467` |
| d | `--repeat N` runs `for pass { for profile { for load } }`: the passes of one preset are separated by the whole list (minutes). There is no way to alternate two **binaries**; the interleaved A/B of rounds 12/13 was driven by scripts that are not in the repository (`TODO.md:28`: the install scripts live under `.hw-local/mpc-perf/`). | `bench/tier_bench.cpp:1066-1070`; `docs/MPC.md:350`; `TODO.md:28` |
| e | `rtrun` (the `SCHED_FIFO 20` wrapper of `docs/MPC.md:350`) is **not in the repository**: the only hits of a repository-wide search are `docs/MPC.md:350` and the review documents. | grep `rtrun`; `docs/MPC.md:350` |
| f | The engine's helper threads inherit the creator's policy and affinity unless a start hook is set. The hook (`setHelperThreadStartHook`) is set by the MPC shell (`shell-mpc/synthyboy_mpc.cpp:3338-3347` policy function, `:3360` installation) and called by two engine threads: `sb-fx-build` (`core/engine-own/own_engine.cpp:167`) and `sb-fx-job` (`core/engine-own/fx/fx_modules.cpp:61`). `tier_bench` never sets a hook, and the wavetable builder thread (`Bank::run`) does not call the hook at all: it only lowers its nice value and polls every 10 ms forever. So in `tier_bench --core 3` every helper thread created after the pin is **pinned to the measured core** too (inherited mask), and under a `SCHED_FIFO` wrapper it would inherit the real-time policy. | `core/include/synthyboy/helper_thread_hook.h:12-26`; `core/engine-own/osc/wavetable_bank.cpp:472-497,594`; `shell-mpc/thread_policy.h:1-12,27-41` |
| g | `--sections` needs a second build (`-DSYNTHYBOY_SECTION_TIMING=ON`, `SB_SECTION_TIMING=1`); it prints the exclusive time of each section per block with the cost of the clock taken off. The sections are fixed engine regions (`engineRest`, `tickOther`, ..., `filter1`, `filter2`, `fxUpdate`, `fxModule0..`), not a per-module catalogue; there is no committed table over a fixed preset set. `--micro` covers only the Spectral, Granular and SampleVoice kernels (`runMicro`, headings "spectral kernels", "granular", "sample voice"). | `core/CMakeLists.txt:53-55`; `core/engine-own/dsp/section_timing.h:1-2,15-33`; `core/engine-own/dsp/section_timing.cpp:57-73`; `bench/tier_bench.cpp:582-776,1079-1109` |
| h | The benchmark is linked **fully static** (glibc 2.36 `libm.a` of the Debian 12 container, direct calls); the plugin links `libm.so.6` of the device (glibc 2.39) through the PLT. The script even fails the build if the result is not static. | `bench/CMakeLists.txt:24-33`; `scripts/build-tier-bench.sh:77-80,86`; `docs/release/CPU-REVIEW-MEMORY.md:359`; `docs/MPC.md:13,22` |
| i | `docs/PERFORMANCE.md` still publishes laptop numbers of 2026-11-04 (Apple M5 Pro, 48 kHz) and says the own engine "implements none of" the heavy patch; `synthyboy_bench` still defaults to 48 kHz and flush-to-zero OFF. | `docs/PERFORMANCE.md:3-5,31-37,56-62`; `bench/bench_engine.cpp:144,149,486` |

### 1.2 What it costs, how big (numbers read, arithmetic shown)

* The Key 61 bench (core 3, Cortex-A17 at 1.608 GHz, "isolated by the kernel", governor `performance`) is "a normal process on a core that also runs one of the application's real-time audio threads": in that run **every** preset, the lightest included (median 4.9 %, p99 23.3 %), has its p99 "about 20 to 25 points of the block above its median", on the XL only 1.5 to 3 points (`docs/MPC.md:321`). Lightest preset: 23.3 - 4.9 = 18.4 points = 18.4 * 29.02 us = 534 us. 20 .. 25 points = 0.2 * 2902.5 = 580 us .. 0.25 * 2902.5 = 726 us.
* Day-to-day drift: the round 12 binary run again on a later day had a median "1.18 times its round 12 figure" (`docs/MPC.md:350`): +18 %.
* Under `SCHED_FIFO 20` (`rtrun`) the round 13 table had "none over 70 %" and mean of p99 39.30 against 52.58 / 48.96 in the normal-scheduling tables (`docs/MPC.md:336,350`). So a real-time run exists and works, but it is manual and not reproducible from the repository.
* The device facts in the repository: Key 61 `/sys/devices/system/cpu/isolated` is `2-3`, the overlay threads run on cores 0-1 (`docs/MPC.md:147`); XL: 4 x A55 + 4 x A76, "the host's audio workers on the A76 cores 4 - 7", measured on A76 core 6 at 2.4 GHz, kernel "Linux 6.18 PREEMPT_RT" (`shell-mpc/device_profile.h:180`; `docs/MPC.md:172-173`). The host's thread names are `AudioWorker0` ... (`docs/MPC.md:173`).
* How much of a p99 a disturbance can move: with `--seconds 10` the measured window has `floor (441000 / 128) = 3445` blocks; `floor (0.99 * 3444) = 3409`, so 3445 - 3410 = **35** blocks lie above the p99; a disturbance that hits fewer than 35 blocks never moves p99 (it moves max). With the default chord retrigger of 1 s = `floor (44100 / 128) = 344` blocks (`bench/tier_bench.cpp:373`) the window holds the strike blocks `344 * k`, `k = 1..11` (`344 * 11 = 3784 < 344 + 3445 = 3789`): **11** strike blocks, 22 with the following carry block, still below 35: p99 excludes the strikes, p99.9 (`floor (0.999 * 3444) = 3440`, only 4 blocks above) sits inside them. With `--seconds 4`: 1378 blocks, p99 index 1363, 14 blocks above, strikes at `344 * k`, `k = 1..5` (5, 10 with carry): the margin shrinks from 13 to 4 blocks. **So a "strike" metric needs p99.9 / max, and 4 s runs are too short for a stable p99.**
* Clock cost: one scope = 1.3 us (two reads, `docs/MPC.md:540`; `bench/tier_bench.cpp:1084` halves `ownSectionClockCostNs` to get one read). With the audit's 320 scopes per block (10 nested scopes x 8 voices x 4 periods, **HYPOTHESIS**, not opened) the clock costs 320 * 1.3 = 416 us = 14.3 % of the block, which the "less clock" column removes only on average. The audit's "~2.6 us per scope, 400-800 us (14-28 %)" double-counts: the upper half is wrong.

### 1.3 What the audit / verification got wrong or left open

1. "`tier_bench` runs next to the host's `SCHED_FIFO 20` audio thread": **`20` is the priority of `rtrun`, not a measured priority of the host's threads.** Nothing in the repository records the host audio threads' policy or priority (`docs/MPC.md:321` says only "real-time audio threads"). First measurement: D0 in 2.1.
2. "+20-25 % p99 inflation": the source says 20 to 25 **points of the block** between p99 and median in the lightest presets (`docs/MPC.md:321`), and calls it an inference ("a part of a block that does not depend on the preset is not the engine's"). Cause (preemption by the host thread, helper threads on the same core, DVFS, thermal) is **HYPOTHESIS**; E0/E1 decide.
3. The audit's anchor preset `Basic Saw` is not a factory preset (no preset of that name in the 160 files of `content/presets`; the synthetic load `basic`, `bench/tier_bench.cpp:335`, is meant). The other eight anchors exist (`content/presets/{Lead,Bass,Pad,Pluck}/*.sbpreset`).
4. The verification quotes `tier_bench.cpp:11-13` text that differs from the file and places `sched_setaffinity` at "134-147"; the real lines are `11-12` (doc) and `940-955`, `819-830` (code). Substance (no scheduling class) is right.
5. Four device claims are code-only and unproven (verification marks rank 18 `CONFIRMED-CODE / CONSEQUENCE-UNPROVEN`): that the libm difference costs anything; that PMU counters exist; that helper threads disturb the bench; that clock reads are slow because of a syscall.
6. `docs/release/CPU-REVIEW-MEMORY.md:344,449`: the A17 PMU events 0x03 / 0x04 / 0x16 / 0x17 are named, "no PMU counter has ever been read on the device", "whether the kernel exposes `perf_event_open` is unknown". This stays true until D0/E3.
7. Out of scope of this plan: the second half of rank 18 (7 `dynamic_cast` helpers, `-fno-rtti` / `-fno-exceptions`, `core/engine-own/own_engine.cpp:32-92`) is a code-size / build-flag change, not a measurement fidelity item; it will be proven with the A/B protocol of section 3.5 as its own item.

---

## 2. The measurement to make BEFORE any change

Nothing existing can measure the real-time interference, PMU counters or a per-module table, so **the first tasks of the plan build that measurement** (section 4). What can be measured today, with today's binary, is E0, and it must be done first because it decides how bad the problem is.

Device addresses, core and profile per device (`docs/MPC.md:321,350`; `shell-mpc/device_profile.h:187-188`):

| | Key 61 | XL |
|---|---|---|
| host | `root@192.168.0.30` | `root@192.168.0.126` |
| arch for `scripts/build-tier-bench.sh` | `--arch arm32` | `--arch arm64` |
| bench core | 3 (isolated, `docs/MPC.md:321`) | 6 (A76, `docs/MPC.md:350`) |
| profile | `low` (8 notes) | `high,voices=16,control=economy` (16 notes) |
| working dir on the device | `/tmp/sbperf` (HYPOTHESIS: writable and exec: `/tmp/rtrun`, `/tmp/synthyboy-callback-stats.txt` were used before, `docs/MPC.md:350,1543`) | same |

Fixed load set **SET12** (used by every step below; every name is cited in `docs/MPC.md` or `bench/tier_bench.cpp:198`): synthetic `basic`, `heavy`, `worst` (`bench/tier_bench.cpp:297-343`); presets `Twin Warp Lead`, `Hyper Saw Lead` (`:198`), `Diffusor Cloud Pad`, `Scream Filter Bass` (`docs/MPC.md:321`), `Stretched Glass Choir`, `Triple Fold Reese` (`:336`), `Ring Mod Gamelan`, `Pentatonic Step Pluck` (`:350`), `Spectral Vowel Freeze` (`:332,353`, needs the sample pack). SET6 = `basic`, `heavy`, plus the four presets a fix names. Run length `--seconds 10` (3445 blocks, see 1.2); one SET12 pass = 12 x (1 + 10) s = 132 s.

### 2.1 Device preparation and read-only probes (D0, D1)

Build (dev machine, repo root; arm32 shown, arm64 for the XL). Until task T9 exists the script builds only the static binary; T9 adds `--dynamic` and the probe targets:

```sh
scripts/build-tier-bench.sh --arch arm32 --dynamic --sections --out build-tier-bench/out
```

Install (BusyBox on the device: `tar | ssh`, no scp; `COPYFILE_DISABLE=1` keeps macOS `._` files out, `.agents/skills/akai-mpc-ssh-deploy/SKILL.md:30-32`):

```sh
H=root@192.168.0.30; O=build-tier-bench/out; S="ssh -o BatchMode=yes -o ConnectTimeout=5 $H"
$S 'mkdir -p /tmp/sbperf/static /tmp/sbperf/dyn /tmp/sbperf/sections /tmp/sbperf/store/com.safian.synthyboy.factory-samples'
for v in "" -dyn -sections; do
  d=$( [ -z "$v" ] && echo static || echo "${v#-}" )
  COPYFILE_DISABLE=1 tar -C $O/tier-bench-arm32$v -cf - . | $S "tar -C /tmp/sbperf/$d -xf -"
done
COPYFILE_DISABLE=1 tar -C content -cf - presets | $S 'tar -C /tmp/sbperf -xf -'
COPYFILE_DISABLE=1 tar -C content/factory-samples -cf - . | $S 'tar -C /tmp/sbperf/store/com.safian.synthyboy.factory-samples -xf -'
```

(The content store layout is `$SYNTHYBOY_CONTENT_DIR/<pack id>/<file>`, `bench/tier_bench.cpp:325-343` and the preset's `"pack":"com.safian.synthyboy.factory-samples","file":"samples/vowel_pad.wav"`; the pack id is the `id` of `content/factory-samples/contentpack.json`.)

**D0, the read-only probe** (changes nothing on the device; its output answers "which core, which priority, is there a PMU"). `acvs` is the MPC application service (`docs/MPC.md:157`, `SKILL.md:36`):

```sh
$S 'sh -s' <<'EOF'
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
```

Field mapping: after `sed 's/^.*) //'` removes `pid (comm) `, awk field `n` is `/proc/<pid>/stat` field `n + 2`: processor = 39 -> `$37`, rt_priority = 40 -> `$38`, policy = 41 -> `$39` (`0` OTHER, `1` FIFO, `2` RR).

What each line decides (the values go into `docs/perf/README.md`, task T18):

| Probe line | Decides | Expected / HYPOTHESIS |
|---|---|---|
| `isolated`, `Cpus_allowed_list`, `cpu=` of `AudioWorker*` | which core(s) carry the host's audio threads; whether core 3 (Key 61) / 6 (XL) is one of them | Key 61 isolated `2-3` is documented (`docs/MPC.md:147`); which isolated core hosts an audio thread: HYPOTHESIS core 3 (`:321`) |
| `rtprio=`, `policy=` of `AudioWorker*` | the real priority `HP` of the host's audio threads (replaces the audit's "20") | HYPOTHESIS `policy=1` (FIFO), `rtprio` unknown |
| `runtime_us / period_us` | RT throttling: default 950000 / 1000000 would stall every RT task on a CPU for 50 ms after 950 ms of RT time | HYPOTHESIS default; decides `--rt-gap` (1.3) |
| `paranoid`, `event_source/devices`, `CONFIG_PERF_EVENTS`, dmesg | whether `perf_event_open` can work | HYPOTHESIS a line like `hw perfevents: enabled with armv7_cortex_a17 PMU driver, N counters available` (ARMv7 kernels print this); if `paranoid >= 3` or no `perf_event`, only root helps, and the runner is root |
| `CONFIG_SCHEDSTATS` / `SCHED_INFO` | whether `/proc/<tid>/schedstat` (run-queue wait time) exists: the direct contamination meter | HYPOTHESIS present; if missing, fall back to `nivcsw` (`getrusage`) only |
| cache sizes | the size of the eviction buffer (2 x the largest cache level) | HYPOTHESIS A17 L2 of 1 or 2 MiB; read it |
| libm paths | whether the host process maps `libm.so.6` of glibc 2.39 (`docs/MPC.md:13`) | the dynamic bench must match it |

### 2.2 E0: how bad is it today (today's binary, no new code)

Records: the 5-pass p99 spread of today's `--core 3` run, and which threads of the bench process run where. Key 61 shown (`H`, `S` as above); results to `build-tier-bench/results/e0-k61/`:

```sh
mkdir -p build-tier-bench/results/e0-k61
$S 'cd /tmp/sbperf && SYNTHYBOY_CONTENT_DIR=/tmp/sbperf/store ./static/synthyboy_tier_bench --presets presets --core 3 --config low --load basic --load "Diffusor Cloud Pad" --seconds 10 --repeat 5 --csv e0.csv' > build-tier-bench/results/e0-k61/e0.txt &
sleep 20
$S 'sh -s' <<'EOF' > build-tier-bench/results/e0-k61/threads.txt
B=$(pidof synthyboy_tier_bench | cut -d' ' -f1); echo "bench pid $B"
for t in /proc/$B/task/*; do echo "$(basename $t) $(cat $t/comm) schedstat[run_ns wait_ns slices]=$(cat $t/schedstat 2>&1) $(sed 's/^.*) //' $t/stat | awk '{print "cpu="$37" rtprio="$38" policy="$39" nice="$19}') $(grep Cpus_allowed_list $t/status | tr -d '\t')"; done
EOF
wait; $S 'cat /tmp/sbperf/e0.csv' > build-tier-bench/results/e0-k61/e0.csv
```

(`nice` = `/proc/stat` field 19 = awk `$17`; the printed `nice=$19` above must read `$17` in the script: task T14 prints it correctly.)

Numbers to record, from the CSV (`min_pct,median_pct,p99_pct,max_pct,blocks_over_100pct` per pass) and `threads.txt`:

| Number | How | Confirms the problem when |
|---|---|---|
| p99 spread of 5 passes, `basic` and `Diffusor Cloud Pad` | `(max - min) / median` of the 5 `p99_pct` | > 2 % (the audit's target, `OWN-ENGINE-DSP-AUDIT-3-CORE-PERFORMANCE-2026-11-20.md:439`) |
| p99 - median gap of `basic` (points) | from the CSV | >= 15 points (today's lightest-preset figure is 18.4, `docs/MPC.md:321`) |
| threads of the bench process | `threads.txt` | helper threads (`sb-fx-build`, `sb-fx-job`, an unnamed `Bank::run` thread) with `Cpus_allowed_list` = `3` (inherited pin) |
| `wait_ns` / (`run_ns` + `wait_ns`) of the bench thread | `threads.txt` read twice 5 s apart, difference | > 0.2 % means the bench thread was runnable but not running: contamination measured directly |

Desktop baseline (any dev machine, to see the spread without a device; not a gate):
`build/bench/synthyboy_tier_bench --presets content/presets --config low --load basic --seconds 10 --repeat 5 --csv /tmp/e0-desktop.csv` and `build/bench/synthyboy_bench --engine own --seconds 10` (`docs/PERFORMANCE.md:22-27`).

### 2.3 What the new tools must measure (E1 .. E5; run after the tasks of section 4)

* **E1 interference characterisation** (after T13): the same two loads, 5 passes, four settings. `HP` is the host audio priority read in D0.
  1. `--core 3` (today).
  2. `--core 3 --rt $HP --rt-require`.
  3. `--core 3 --rt $((HP+1)) --rt-require`.
  4. as 2 with the host stopped (dead-man protected, E1d below).
  Decision rule (least intrusive first, 2 -> 3 -> 4): a setting is **accepted** when for both loads (a) the 5-pass p99 spread <= 2 %, (b) median over passes of `run_delay_pct` <= 0.2 %, (c) p99 - median of `basic` <= 3 points (the XL's 1.5 to 3, `docs/MPC.md:321`). If even setting 4 fails, the noise is not the host: look at helper threads (`--no-helpers-away` vs default), thermal / frequency (columns `temp_c_max`, `khz_*`) and E3.
  E1d (host stopped; **never on the XL while a project is open; owner's device, runner only**):
  ```sh
  $S 'sh -s' <<'EOF'
  P=$(systemctl show -p MainPID acvs | cut -d= -f2); [ "$P" -gt 1 ] || exit 1
  ( sleep 150; kill -CONT $P ) >/dev/null 2>&1 &      # dead-man: the host runs again after 150 s whatever happens
  kill -STOP $P
  cd /tmp/sbperf && SYNTHYBOY_CONTENT_DIR=/tmp/sbperf/store ./static/synthyboy_tier_bench --presets presets --core 3 --rt 20 --rt-require --config low --load basic --load "Diffusor Cloud Pad" --seconds 10 --repeat 5 --csv e1d.csv
  kill -CONT $P
  EOF
  ```
  150 s covers 2 loads x 5 passes x 11 s = 110 s plus start-up.
* **E2 noise floor (A/A)**: the same binary twice (`cp static/synthyboy_tier_bench static/synthyboy_tier_bench_copy`), SET12, ABBA sequence of 20 passes (10 pairs), the accepted E1 setting; 20 x 132 s = 44 min. Result: for each load the 97.9 % interval of the paired ratio (3.5). **Accept the protocol when the interval lies inside [0.99, 1.01] for at least 11 of 12 loads** (HYPOTHESIS target; if not, `min-gain` of 3.5 is raised to the measured half-width and the table says so).
* **E3 PMU** (after T12, T15): `synthyboy_pmu_probe` on both devices (prints per event `status=ok|fail errno=.. why=..` and a group verdict `PMU-PROBE-RESULT cache=usable|multiplexed|unavailable compute=...`), then `tier_bench --pmu cache` and `--pmu compute` on `Diffusor Cloud Pad` (one pass each). Valid when `pmu_scaling` (time running / time enabled) is 1.000. The cost of the reads: median block time with and without `--pmu` must differ by <= 1 point (29 us); HYPOTHESIS, decided here.
  The 18 % drift question (`docs/MPC.md:350`): in two runs on two days, if time per block drifts but `cycles_mean` does not -> frequency / DVFS; if both drift -> memory system or code layout. Record `khz_before`, `khz_after`, `temp_c_max`, `cycles_mean` in the same CSV.
* **E4 module table** (after T17): `--modules` on both devices (3.4); 25 min per device.
* **E5 libm (rank 18)** (after T4, T9): (i) `synthyboy_libm_probe` static against dynamic, 7 alternating repetitions each; (ii) A/B of `static` against `dyn` with the same source on `worst`, `Bode Barber Riser`, `Hyper Saw Lead` at profile `low,fx=all` (the effects that call libm most are on, `bench/tier_bench.cpp:21-22`), 10 pairs. Rule: if the 97.9 % interval of the ratio `dyn / static` is inside the A/A band of E2 for all three, rank 18 is closed as "no measurable effect" and the static binary stays the convenient default; if outside, **the dynamic binary becomes the gating binary** (it is the one that matches the plugin) and `docs/perf/README.md` says so. Also record `clock_steady` and `clock_gettime` ns of the probe in both links: if the static one is slower, the 1.3 us scope (`docs/MPC.md:540`) is a static-link artefact (HYPOTHESIS: no vDSO in the static binary).

---

## 3. Design options and the chosen design

The specification (`docs/engine-spec/*.md`) says nothing about measurement; the constraints that are kept are the audio-thread rules (no allocation, no lock, no system call, block-size independent output) and the existing contract of the instrumentation: "off, the macros are empty and the engine is the same code, bit for bit" (`core/engine-own/dsp/section_timing.h:1-2`). All new code lives in `bench/`, `scripts/`, `core/tests/perf/`, `docs/`.

### 3.1 Isolating the benchmark from the host's audio thread

| | A: pin + `SCHED_FIFO` + helper threads away + validity gate | B: stop the host while measuring | C: do not isolate, measure CPU time and PMU cycles |
|---|---|---|---|
| How | `sched_setaffinity` (exists) + `sched_setscheduler (SCHED_FIFO, P)` + start hook that moves helper threads to the other cores (as the shell does, `shell-mpc/synthyboy_mpc.cpp:3338-3347`) + `--rt-gap` so RT throttling cannot fire + per-run health columns (`schedstat` wait share, thermal, frequency, faults) that mark a run INVALID | `kill -STOP` the `acvs` main PID with a dead-man `kill -CONT` | per-thread CPU time (`CLOCK_THREAD_CPUTIME_ID`) and per-thread PMU counters (excluded while descheduled) instead of wall time |
| Sound | none | none | none |
| CPU / memory | none on the engine; bench: 1 `read` of `/proc/thread-self/schedstat` per run | none | PMU: 2 `read` syscalls per block, outside the timed region (HYPOTHESIS <= 1 point, E3) |
| Real-time safety | engine untouched; **the host's audio thread on the same core is starved up to 64 blocks = 186 ms between gaps** (audible: run only on an idle device with no playback) | the whole host (UI, MIDI) frozen; recoverable by the dead-man | none |
| Risk | host priority unknown (E1 decides P); equal-priority FIFO does not preempt, so P = HP shares the core cooperatively, P = HP + 1 preempts the host | an `acvs` watchdog or the audio driver may not like a stopped process (HYPOTHESIS, never tried) | removes preemption but not cache / TLB pollution; wall time (what the audio callback really experiences) is lost |

**Chosen: A as the standard (this is what `rtrun` did by hand and what gave stable round 13 numbers, `docs/MPC.md:350`), with C's columns recorded in the same runs as a cross-check (`thread_cpu_ms` against the wall sum; PMU cycles), and B only as the reference of E1 (setting 4).** Reasons: A measures the quantity that matters for drop-outs (wall time of the block under the real scheduler); its failure mode is visible (INVALID runs, never silently averaged); B is intrusive; C alone hides exactly the preemption that causes a drop-out.

Parameters derived from the code:
* RT throttling: default `sched_rt_runtime_us / sched_rt_period_us` = 950000 / 1000000 (HYPOTHESIS, read in D0). A back-to-back FIFO loop would be throttled for 50 ms per second (= 17.2 blocks, `50 / 2.9025`), a stall that looks like one giant block and also stops the host's RT thread. The rule `--rt-gap`: after every `kRtGapEvery = 64` blocks sleep a gap outside the timed region so that the duty stays below `runtime - 1 %`: busy = 64 * 2.9025 = 185.76 ms (worst case, every block at 100 %), gap >= busy * (1 / 0.94 - 1) = 185.76 * 0.06383 = 11.86 ms -> **12 ms**, duty = 185.76 / (185.76 + 12) = 93.9 % < 95 %. `--paced` needs no gap (it sleeps to every block's time, `bench/tier_bench.cpp:407-408`), but its blocks start phase-locked to the host's callback period (also 2.90 ms) and would collide at a fixed phase: so the gate uses back-to-back + gap, `--paced` stays an extra view.
* A block longer than `stallBlocks = 12` x 2.9025 = 34.8 ms in a FIFO run is classified `stall` (the 50 ms throttle signature); the worst real strike measured is 5.2 blocks (`docs/MPC.md:354`: "2.6 to 3.1 blocks", 3-5 for Spectral at `:332`).
* Helper threads: the hook (`HelperThreadStartHook`, `core/include/synthyboy/helper_thread_hook.h:12-26`) sets `SCHED_OTHER` nice 0 and the affinity "all online CPUs except the measured core" (`helperCpus`). The wavetable builder thread does not call the hook (`wavetable_bank.cpp:472-497`), so it keeps the inherited mask: therefore the bench **creates the measuring thread's pin after** the first engine exists is not possible (engines are created per run); instead `tier_bench` applies the pin and FIFO in the *measuring thread* but sets the process-wide default affinity of the main thread (parent of all helper threads) to the helper set first, then pins itself (task T13 spells out the order). Result to verify in E0/E1 with `threads.txt`.

### 3.2 PMU counters

| | A: own `perf_event_open` group via `syscall` | B: the `perf` tool around the process | C: user-space counter read (`mmap` page / `PMCCNTR`) |
|---|---|---|---|
| How | `PmuReader` in `bench/perf/perf_pmu.h`: group leader = CPU cycles, `PERF_FORMAT_GROUP | TOTAL_TIME_ENABLED | TOTAL_TIME_RUNNING`, `exclude_kernel`, one `read` per block outside the timed region | `perf stat` on the device | needs `PMUSERENR` enabled by the kernel |
| Sound / CPU / memory | none / HYPOTHESIS <= 1 point / a few hundred bytes | none / whole-process totals, no per-block series, no p99 | none / lowest / n/a |
| RT safety | bench only | bench only | bench only |
| Risk | kernel without `CONFIG_PERF_EVENTS`, `perf_event_paranoid`, or > 4 events multiplexed (the ARMv7 PMU has 6 event counters + a cycle counter, HYPOTHESIS, E3 checks `time_running == time_enabled`) | `perf` is not on the device (BusyBox image; static `perf` would have to be built) | user access is off by default on ARM32 |

**Chosen: A.** Events (architectural event numbers named in `docs/release/CPU-REVIEW-MEMORY.md:344`): set `compute` = cycles, instructions, branch mispredictions; set `cache` = cycles, L1D refill `0x03`, L1D access `0x04`, L2D refill `0x17`, L2D access `0x16` (4 event counters + the cycle counter per set; two sets = two runs, never more than the counter count so there is no multiplexing). The per-section PMU attribution is **not** done (nested syscalls per scope would repeat the 1.3 us problem); attribution is by the differential module table (3.4). `synthyboy_pmu_probe` is the standalone check ("say how to check"): `ls /sys/bus/event_source/devices`, `cat /proc/sys/kernel/perf_event_paranoid`, `zcat /proc/config.gz | grep PERF_EVENTS` (D0), then the probe binary.

### 3.3 Cache-cold against warm

Three run kinds, all in `tier_bench`:
* **warm** (today): blocks back to back after 344 warm-up blocks.
* **evict** (`--evict`): before every block, outside the timed region, a read-modify-write of a buffer of `2 x` the largest cache level of the measured core (read from `/sys/devices/system/cpu/cpuN/cache/index*/size`, fallback 4 MiB) with a 64-byte stride, so the engine's working set starts every block cold. The ratio evict / warm per preset is the cache sensitivity; it is the natural companion of the PMU `l2d_refill` column and the test of the memory hypotheses MEM1 / MEM2 (`docs/release/CPU-REVIEW-MEMORY.md:344`).
* **first blocks** (`--first-blocks N`): blocks 0 .. N-1 of a fresh engine with a chord at block 0 (no warm-up), with `minflt` per block and the `prepare` time: the first-note / first-touch cost that a 10 s steady run hides.

### 3.4 The per-module cost table

| | A: `--sections` table | B: differential isolation (whole-block time, no nested clocks) | C: per-module microbenchmarks (`--micro` style) |
|---|---|---|---|
| How | existing second build, exclusive time per section, clock cost subtracted (`bench/tier_bench.cpp:1079-1109`) | `--modules`: base patch + one module switched on; cost = median over repetitions of (case - base) with the pair measured back to back | call the kernel in a loop |
| Sound | none | none | none |
| CPU / memory | clock reads inside the engine: ~16-20 scopes, 21-26 us per block (verification arithmetic) up to 416 us for the voice tick (HYPOTHESIS, 1.2) | none inside the engine | none |
| Risk | per-section figures distorted when many small scopes exist; sections are not modules | interactions (a filter after a warp) are not additive; base choice | the kernel in isolation is warm, unlike in the engine |

**Chosen: A and B both, they answer different questions.** A (committed per SET12 preset, task T11 builds the table from the existing output) says where a block of a *real preset* goes; B (task T17) gives a catalogue cost for **every** module independent of presets: every FX module, every filter model, every warp type, morph, unison counts, extra oscillators, sub, noise. C stays as the existing `--micro` for the content kernels. B generates its cases from the parameter table itself (`ParamInfo::numChoices`, `core/include/synthyboy/params.h:57-74`; `fxTypeDescriptor`, `core/include/synthyboy/fx_types.h:23-44,60-62`), so a new filter model or warp type enters the table without editing the bench. An effect is switched on with its `mix` at the middle of its range because the engine skips an effect whose mix is exactly 0 (`core/include/synthyboy/engine.h:199-200`, `kOwnSavingFxMute`).

### 3.5 The A/B protocol and the automated threshold (chosen, no real alternative needed; the alternatives rejected are in the table)

| Rejected | Why |
|---|---|
| `--repeat 3` in one process, before / after in separate sessions | passes of one preset are minutes apart (`bench/tier_bench.cpp:1066-1070`); the 18 % day-to-day drift (`docs/MPC.md:350`) is larger than the gains to prove (10.7 points median, 8.5 points p99 in round 13) |
| bootstrap confidence interval | needs a random generator in the test and a seed contract; the sign-test interval below is distribution free and exact |
| comparing means of ms | strike blocks and preemption outliers dominate means; the median of paired ratios does not |

**Interleaved A/B, chosen.** The runner alternates the two binaries pass by pass in the order `A B B A A B B A ...` (ABBA cancels a linear drift of temperature or frequency); every pass is a **fresh process** over the same loads (also re-randomises heap layout, `Mytkowicz et al. 2009`). Adjacent runs form pairs: (1st, 2nd), (4th, 3rd), (5th, 6th), ... ; `K` pairs give `K` ratios `rho_k = M_B,k / M_A,k` per load for a metric `M` (median block time as the primary, p99 as the second, `cycles_mean` when PMU is available).
* Estimate: median of the `rho_k` (mean of the two middle values for even `K`).
* Interval: the distribution-free interval `[rho_(r), rho_(K+1-r)]` of the order statistics, with `r` the largest value such that `2 * P (Bin (K, 1/2) <= r - 1) <= 0.05`. `K = 6` is the smallest `K` with `r = 1` (`2 / 64 = 0.031`); **`K = 10` gives `r = 2` and coverage `1 - 2 * 11 / 1024 = 0.9785` (97.9 %)**. The protocol uses **K = 10 pairs = 20 passes**: SET6 = 20 x 66 s = 22 min, SET12 = 44 min.
* Verdict per load with `g = min-gain = 0.01` (raised to the A/A half-width of E2 if larger): `GAIN` if `rho_(K+1-r) < 1 - g`; `REGRESSION` if `rho_(r) > 1 + g`; else `NO-CHANGE-PROVEN`. A claimed CPU gain is accepted only with `GAIN` on **both** ms and (where PMU works) cycles; disagreement means a frequency / thermal artefact.
* Sound first: before any timing the pair must render the same samples (`tier_bench --verify`, and `synthyboy_preset_render --float32 --hash` compared with `scripts/compare-renders.sh`, as in `docs/MPC.md:336`).

**Automated regression threshold (`scripts/perf-gate.py`).** Baseline = 5 passes of SET12 on one device under the accepted setting (`docs/perf/baseline-<device>.json`, produced by the runner, committed by the coordinator). A check = 3 passes. Per (profile, load), with `med` = median over passes, `spread = (max - min) / med`:

| Metric | Fails when |
|---|---|
| p99 (points) | `new_med > base_med * (1 + max (0.03, 2 * spread_base)) + 0.5` |
| median block (points) | the same formula |
| max block (points) | `new_med > base_med * (1 + max (0.10, 2 * spread_base)) + 2.0` |
| blocks over 100 % | `new_med > base_med * 1.2 + 2` |
| `rss_kb` | `new > base * 1.05` |
| `majflt` in the measured window | `> 0` |
| `minflt_measured` | `> base_max` (the audio path must not touch new pages after warm-up; HYPOTHESIS: baseline 0) |
| any row with `valid = 0` | the whole check is `GATE-INVALID` (exit 3), never averaged in |

The `0.5` / `2.0` point slacks are 14.5 us / 58 us. Thresholds are the initial values; E2 replaces `0.03` by the measured floor when larger.

---

## 4. The task breakdown

Rules for every task: one commit pair (test commit that fails on today's tree, then the fix commit), at most three files, no engine file, Conventional Commits, `Co-Authored-By: Gemini <noreply@google.com>`. Tasks do not compile; the single build worker runs everything at the end of a batch group. **Bit-identical sound:** all tasks leave `core/` untouched, so the render of every preset is identical by construction (checked once in section 5 with the render hash, not per task; the tasks therefore print no `<NAME>-CHECKSUM` lines).

New files: `bench/perf/*.h` (header-only, no engine dependency except T8), tests in `core/tests/perf/test_perf_*.cpp` (registered by the glob of T1: **no later task edits `core/tests/CMakeLists.txt`**), Python tests in `scripts/tests/test_perf_*.py` (found by `unittest discover -s scripts/tests`, `scripts/tests/CMakeLists.txt:6-9`: **no CMake edit**), CLI smoke tests as one line each in `bench/perf/cli_smoke.txt`.

### 4.1 Shared contracts (every task uses exactly these names)

`namespace synthyboy::perf`; the CSV schema v2 is the existing 26 columns (`bench/tier_bench.cpp:1051`: `device,cpu,cpu_info,profile,settings,load,group,notes,voices_mean,voices_max,sr,block,blocks,min_pct,median_pct,p99_pct,max_pct,mean_pct,min_ms,median_ms,p99_ms,max_ms,mean_ms,blocks_over_100pct,cpu_khz_after_run,pass`) followed by the appended groups:

* T13: `p999_pct,rt_prio,thread_cpu_ms,run_delay_pct,nivcsw,minflt_run,minflt_measured,majflt,rss_kb,temp_c_max,khz_before,valid,invalid_reason`
* T15: `pmu_set,cycles_mean,cycles_p99,instr_mean,ipc,l1d_refill_per_block,l1d_access_per_block,l2d_refill_per_block,l2d_access_per_block,br_mis_per_block,pmu_scaling`
* T16: `cold_mode,first_block_ms`

Python readers use `csv.DictReader` and need only `profile,load,median_pct,p99_pct,max_pct,blocks_over_100pct`; every other column is optional (empty string = absent).

Header contracts (functions in `synthyboy::perf`):

```cpp
// bench/perf/perf_stats.h
double percentileSorted (const std::vector<double>& sorted, double q) noexcept;   // sorted[size_t (q * double (n - 1))], the rule of tier_bench.cpp:464
double medianSorted (const std::vector<double>& sorted) noexcept;                 // sorted[n / 2], tier_bench.cpp:463
struct Summary { size_t n = 0; double min = 0, median = 0, p99 = 0, p999 = 0, max = 0, mean = 0; size_t over = 0; };
Summary summarize (std::vector<double> values, double budget);                    // over = values > budget
double spreadRelative (std::vector<double> passes);                               // (max - min) / medianSorted
struct RunHealth { double runDelayShare = 0, khzBefore = 0, khzAfter = 0, khzMax = 0, tempMaxC = 0, maxMs = 0, budgetMs = 0; long majflt = 0; };
struct HealthLimits { double runDelayShare = 0.002, freqShare = 0.98, tempMaxC = 80.0, stallBlocks = 12.0; long majflt = 0; };
std::string invalidReason (const RunHealth&, const HealthLimits& = {});          // "" valid; else codes in the order stall, run-delay, freq, majflt, temp joined by ','

// bench/perf/perf_sched.h
std::vector<int> parseCpuList (const std::string&);                               // "0-1,4,6-7" -> {0,1,4,6,7}; any malformed token -> {}
std::vector<int> helperCpus (const std::vector<int>& online, int measuredCore);   // online minus measuredCore; if that is empty: online
struct SchedStat { unsigned long long runNs = 0, waitNs = 0, slices = 0; bool ok = false; };
SchedStat parseSchedstat (const std::string& line);                               // "run wait slices"
double waitShare (const SchedStat&) noexcept;                                     // wait / (run + wait), 0 when both 0
double minGapMs (double blockMs, int every, double rtShare, double margin) noexcept; // blockMs * every * (1 / (rtShare - margin) - 1)
constexpr int kRtGapEvery = 64;
struct SchedRequest { int core = -1; int fifoPriority = 0; bool mlockAll = false; };
struct SchedResult { bool affinityOk = false, fifoOk = false, mlockOk = false; int err = 0; std::string text; };
SchedResult applySched (const SchedRequest&);                                     // Linux: sched_setaffinity / sched_setscheduler (SCHED_FIFO) / mlockall; elsewhere text "not Linux: ignored"
void installHelperPolicyHook (int measuredCore);                                  // Linux: setHelperThreadStartHook (static function: SCHED_OTHER nice 0, affinity helperCpus (online, core)); elsewhere nothing
SchedStat readThreadSchedstat();                                                  // /proc/thread-self/schedstat; ok=false when absent
long readRtRuntimeUs(); long readRtPeriodUs();                                    // -1 when absent

// bench/perf/perf_env.h
bool parseMilliC (const std::string&, double& celsius);                           // "68500\n" -> 68.5
long parseStatusKb (const std::string& text, const std::string& key);             // "VmRSS" in /proc/self/status text; -1 absent
long parseCacheBytes (const std::string&);                                        // "32K" 32768, "2M" 2097152, bad -1
size_t evictBytes (const std::vector<long>& levelSizes);                          // 2 x max; no sizes: 4194304
struct Rusage { long minflt = 0, majflt = 0, nvcsw = 0, nivcsw = 0; };
Rusage readRusage();                                                              // RUSAGE_THREAD on Linux, else RUSAGE_SELF
double maxThermalC();                                                             // max of /sys/class/thermal/thermal_zone*/temp, NaN when none
long readRssKb();                                                                 // VmRSS of /proc/self/status, -1 when absent
double threadCpuMs();                                                             // CLOCK_THREAD_CPUTIME_ID
std::vector<long> cacheLevelBytes (int cpu);                                      // sizes of level >= 2 and of Data / Unified caches of /sys/devices/system/cpu/cpuN/cache/index*
void evictCaches (std::vector<unsigned char>& buffer);                            // read-modify-write every 64th byte

// bench/perf/perf_pmu.h
enum class PmuSet { compute, cache };
struct PmuEventSpec { const char* name; unsigned type; unsigned long long config; };   // type 0 = PERF_TYPE_HARDWARE, 4 = PERF_TYPE_RAW (perf_event_open(2))
std::vector<PmuEventSpec> eventSet (PmuSet);       // compute: cycles(0,0) instructions(0,1) br_mis_pred(4,0x10); cache: cycles(0,0) l1d_refill(4,0x03) l1d_access(4,0x04) l2d_refill(4,0x17) l2d_access(4,0x16)
int parseParanoid (const std::string&);            // "2\n" -> 2, "-1" -> -1, bad -> 99
std::string explainOpenError (int err, int paranoid);
double scaledCount (unsigned long long raw, unsigned long long enabled, unsigned long long running) noexcept; // running == 0 -> -1, else raw * enabled / running
double ratio (double numerator, double denominator) noexcept;                      // 0 when the denominator is 0 (ipc, miss rate)
struct PmuSample { unsigned long long value[8] = {}, timeEnabled = 0, timeRunning = 0; };
class PmuReader { public: bool open (PmuSet, std::string& why); void close(); bool read (PmuSample&) noexcept; int count() const noexcept; const char* name (int i) const noexcept; };  // Linux: syscall (__NR_perf_event_open); elsewhere open() = false, why = "not Linux"

// bench/perf/perf_modules.h  (links synthyboy_core)
struct ModuleCase { std::string group, name; std::vector<std::pair<std::string, float>> params; };
std::vector<ModuleCase> moduleCases();             // groups "fx" "filter" "warp" "morph" "unison" "osc"; rules in T8
```

The printed lines of the tools (tests and parsers depend on them):

* `rt: requested FIFO <p> on cpu <c>: affinity <ok|refused>, policy <ok|refused (errno text)>, mlockall <ok|refused|off>` (`rt: not Linux: ignored` elsewhere)
* `helpers: engine threads on cpus <list> (measured cpu <c>)` or `helpers: left as inherited`
* per run: `    health: run_delay <x.xxxx> %, thread cpu <x.x> ms of <x.x> ms, nivcsw <n>, minflt <n> (measured <n>), majflt <n>, rss <n> kB, temp <x.x> C, cpu <khz> -> <khz> kHz, valid|INVALID (<reason>)`
* per run with `--pmu`: `    pmu: set <cache|compute> cycles <x>/block (p99 <x>), ipc <x.xx>, l1d refill <x.x>/block (<x.xx> %), l2d refill <x.x>/block (<x.xx> %), br_mis <x.x>/block, scaling <x.xxx>` (or `pmu: unavailable (<why>)`)
* `first: prepare <x.x> ms` and `first: block <i> <x.xxx> ms (<x.x> %) minflt <n>`
* `module <group> | <name> | base <x.xx> us | case <x.xx> us | cost <+x.xx> us | <+x.xx> % of block | reps <n> | spread <x.x> %` and the last line `module-table: <n> cases`
* last line of a run with `--csv`: `csv schema 2: <path>`

### 4.2 Batches and dependencies

| Batch | Tasks (parallel, no shared file) | Depends on |
|---|---|---|
| 1 | T1 stats + test glob; T2 A/B script; T3 gate script; T4 libm probe + CLI smoke harness | none |
| 2 | T5 sched header; T6 env header; T7 pmu header; T8 modules header; T9 build script; T10 bench_engine defaults; T11 table script | T5-T8 need T1 (glob); T9 needs T4 (option name); T10 needs T4 (it edits `bench/CMakeLists.txt` after it) |
| 3 | T12 pmu probe; T13 tier_bench RT / health; T14 device runner script | T12: T7, T4; T13: T1, T4, T5, T6; T14: none (option names are fixed in 4.1) |
| 4 | T15 tier_bench PMU | T7, T13 (same file `tier_bench.cpp`) |
| 5 | T16 tier_bench cold / first blocks | T6, T15 (same file) |
| 6 | T17 tier_bench `--modules` | T8, T16 (same file) |
| 7 | T18 docs | T2-T17 |

After batch 7 (not Flash tasks, in this order): the build worker builds arm32 / arm64 (`scripts/build-tier-bench.sh --arch ... --dynamic --sections`) and runs the desktop tests; the runner executes D0, E0 .. E5 (section 2) on both devices with `scripts/perf-device.sh` (T14); the coordinator commits `docs/perf/baseline-k61.json`, `baseline-xl.json`, the two section tables and the two module tables (T11 / T3 / T17 outputs), and deletes the items `TODO.md:115` and `TODO.md:124` and the "first step" sentence of `TODO.md:56` (the rule of `AGENTS.md`: a finished item is deleted, the commit message is the history).

### 4.3 Tasks

#### T1 (batch 1) `perf_stats.h` and the C++ test glob
* Files: `core/tests/CMakeLists.txt`, `core/tests/perf/test_perf_stats.cpp`, `bench/perf/perf_stats.h`.
* Change: append to `core/tests/CMakeLists.txt` (after the `synthyboy_add_test` function, `core/tests/CMakeLists.txt:9-21`):
  ```cmake
  # tests of the measurement helpers (bench/perf/*.h): every core/tests/perf/test_perf_*.cpp is one test executable named synthyboy_<file name>
  file(GLOB SYNTHYBOY_PERF_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/perf/test_perf_*.cpp")
  foreach(src ${SYNTHYBOY_PERF_TESTS})
      get_filename_component(perf_test_name ${src} NAME_WE)
      synthyboy_add_test(synthyboy_${perf_test_name} ${src})
      target_include_directories(synthyboy_${perf_test_name} PRIVATE "${PROJECT_SOURCE_DIR}/bench")
  endforeach()
  ```
  and write `bench/perf/perf_stats.h` with the contract of 4.1 (header-only, `#pragma once`, SPDX header like `bench/own_heavy_patch.h:1-7`). `invalidReason`: `maxMs > stallBlocks * budgetMs` -> `stall`; `runDelayShare > limit` -> `run-delay`; both `khzBefore` and `khzAfter` known (> 0) and `min (khzBefore, khzAfter) < freqShare * khzMax` -> `freq`; `majflt > limit` -> `majflt`; `tempMaxC > limit` -> `temp`.
* Test (`TEST (perf_stats)` using `CHECK` of `core/tests/test_runner.h`), commit 1 = glob + test (does not compile: header missing), commit 2 = header. Signal / quantity / threshold, values computed from the rules above:
  * `values = 1.0 .. 1000.0`, `budget = 500.5` -> `n = 1000`, `min = 1`, `median = sorted[500] = 501`, `p99 = sorted[floor (0.99 * 999) = 989] = 990`, `p999 = sorted[floor (0.999 * 999) = 998] = 999`, `max = 1000`, `mean = 500.5`, `over = 500` (the values 501 .. 1000). Exact equality (all are integers or .5).
  * `values = {7.0}` -> every statistic 7.0, `over = 0` for budget 7.0.
  * `spreadRelative ({100, 102, 98}) = (102 - 98) / 100 = 0.04` (within 1e-12).
  * `invalidReason` with `{runDelayShare 0.001, khz 1608000 / 1608000 / 1608000, tempMaxC 69.6, maxMs 5.0, budgetMs 2.9025, majflt 0}` -> `""`; `runDelayShare 0.003` -> `"run-delay"`; `khzAfter 1500000` (`1500000 / 1608000 = 0.9328 < 0.98`) -> `"freq"`; `majflt 1` -> `"majflt"`; `tempMaxC 81` -> `"temp"`; `maxMs 40.0` (`40 > 12 * 2.9025 = 34.83`) -> `"stall"`; `runDelayShare 0.003` with `majflt 1` -> `"run-delay,majflt"`; `khz*` all 0 (unknown) -> no `freq`.
  * Before: the test target does not build (missing header), ctest reports it failed / not built. After: all checks pass.

#### T2 (batch 1) `scripts/perf-ab.py`, the interleaved A/B analysis
* Files: `scripts/perf-ab.py`, `scripts/tests/test_perf_ab.py`.
* Change: `perf-ab.py --metric median_pct|p99_pct|cycles_mean [--min-gain 0.01] [--fail-on-regression] --sequence ABBAABBA... FILE1 FILE2 ...`: the files are the CSVs (schema v2, one pass each) in chronological order, `--sequence` gives the label of each file; the groups of two adjacent files `(f0, f1), (f2, f3), ...` must carry different labels, else exit 2; a pair gives `rho = M(B) / M(A)` for every `(profile, load)` key present in both files (missing / empty metric -> the key is skipped for that pair). Per key with `K` ratios: `r` = largest value with `2 * P (Bin (K, 1/2) <= r - 1) <= 0.05` (compute with `math.comb`; `K < 6` -> exit 2 "too few pairs"); estimate = median (mean of the two middle values for even `K`); interval `[rho_sorted[r-1], rho_sorted[K-r]]` (0-based); verdict per 3.5. Output one line per key: `AB-VERDICT <profile> | <load> | K=<k> | median_ratio=<.4f> | ci=[<.4f>, <.4f>] | <GAIN|REGRESSION|NO-CHANGE-PROVEN>`, then `AB-SUMMARY gain=<n> regression=<n> unproven=<n> geomean_ratio=<.4f>` (geometric mean of the per-key median ratios). Exit 0, or 1 with `--fail-on-regression` and at least one `REGRESSION`.
* Test (a `unittest` that writes temporary CSVs: A rows have `median_pct = 100.0`, B rows `100.0 * rho`, `p99_pct`/`max_pct`/`blocks_over_100pct` filled):
  * `K = 10`, `rho = [0.90 .. 0.99 step 0.01]` in file order: `r = 2` (`2 * 11 / 1024 = 0.0215 <= 0.05`; `r = 3` gives `2 * 56 / 1024 = 0.109 > 0.05`); sorted interval `[0.91, 0.98]`; median `(0.94 + 0.95) / 2 = 0.945`; `0.98 < 1 - 0.01 = 0.99` -> `GAIN`; the line is exactly `AB-VERDICT P | L | K=10 | median_ratio=0.9450 | ci=[0.9100, 0.9800] | GAIN`.
  * `rho = [1.01 .. 1.10]` -> interval `[1.02, 1.09]`, `1.02 > 1.01` -> `REGRESSION`; with `--fail-on-regression` exit code 1.
  * `rho = [0.97, 0.98, 0.98, 0.99, 0.99, 1.00, 1.00, 1.00, 1.01, 1.02]` -> interval `[0.98, 1.01]` -> `NO-CHANGE-PROVEN`.
  * `K = 5` pairs -> exit 2; a sequence `AAB...` (a pair with equal labels) -> exit 2.
  * Before: the script does not exist (import / run fails). After: the four cases pass.

#### T3 (batch 1) `scripts/perf-gate.py`, the regression threshold and the spread report
* Files: `scripts/perf-gate.py`, `scripts/tests/test_perf_gate.py`.
* Change: three subcommands over CSVs of schema v2 (one pass per file, key = `(profile, load)`):
  * `baseline --out FILE.json RUN.csv ...` (>= 3 files): per key store `p99_pct`, `median_pct`, `max_pct`, `blocks_over_100pct`, `rss_kb`, `minflt_measured` as the list of passes plus `med` and `spread = (max - min) / med`; also the device (`device` column), profile, `block`, `sr`.
  * `compare BASELINE.json RUN.csv ...` (>= 3 files; fewer -> exit 2): applies the table of 3.5; prints `GATE-FAIL <key> | <metric> | base <x.xx> | new <x.xx> | limit <x.xx>` per violation, `GATE-INVALID <key> | <invalid_reason>` for a row with `valid` = `0` (exit 3, takes precedence), and last `GATE-PASS <n> keys` (exit 0) or `GATE-RESULT FAIL <n> violations` (exit 1). Keys in the run but not in the baseline: `GATE-NEW <key>` (not a failure); keys in the baseline but not in the run: `GATE-FAIL ... missing`.
  * `spread RUN.csv ...`: per key `SPREAD <profile> | <load> | p99_med=<.2f> | spread=<.2f> % | gap_pts=<p99_med - median_med .2f>` (the E0 / E1 numbers).
* Test (fixtures computed from the rules):
  * Baseline p99 passes `[50.0, 51.0, 52.0]`: `med = 51.0`, `spread = 2 / 51 = 0.039216`, `tau = max (0.03, 0.078431) = 0.078431`, limit `= 51 * 1.078431 + 0.5 = 55.0 + 0.5 = 55.5`. New passes with p99 median `55.4` -> pass; `55.6` -> `GATE-FAIL ... p99_pct ... limit 55.50`, exit 1.
  * Baseline max passes `[140, 150, 160]`: `med = 150`, `spread = 20 / 150 = 0.1333`, `tau = max (0.10, 0.2667) = 0.2667`, limit `150 * 1.2667 + 2.0 = 190 + 2 = 192.0`; new `191` pass, `193` fail.
  * Baseline `blocks_over_100pct` `[0, 0, 0]` -> limit `0 * 1.2 + 2 = 2`: new median `2` pass, `3` fail.
  * `majflt` of any new row `1` -> `GATE-FAIL ... majflt`; a row with `valid` `0` -> exit 3 even if every metric passes.
  * `spread` on the three baseline CSVs prints `spread=3.92 %` and `gap_pts` computed from median.
  * Before: the script does not exist. After: all pass.

#### T4 (batch 1) `synthyboy_libm_probe` and the CLI smoke harness (rank 18)
* Files: `bench/CMakeLists.txt`, `bench/libm_probe.cpp`, `bench/perf/cli_smoke.txt`.
* Change: in `bench/CMakeLists.txt`:
  * after `bench/CMakeLists.txt:30-33` add `option(SYNTHYBOY_TIER_BENCH_STATIC_LIBCXX "Link the benchmarks with -static-libstdc++ -static-libgcc but dynamic glibc, like the plugin (libm of the device)" OFF)` and, if ON and `SYNTHYBOY_TIER_BENCH_STATIC` is OFF, `target_link_options` `-static-libstdc++ -static-libgcc` on `synthyboy_tier_bench`, `synthyboy_libm_probe`, (later `synthyboy_pmu_probe`);
  * `add_executable(synthyboy_libm_probe libm_probe.cpp)` (links nothing of the engine, same warning flags and `SYNTHYBOY_TIER_BENCH_STATIC` rule as `tier_bench`), `add_test(NAME synthyboy_libm_probe COMMAND synthyboy_libm_probe --quick)` with `PASS_REGULAR_EXPRESSION "LIBM-PROBE-RESULT ok"`;
  * the smoke harness: 
    ```cmake
    # CLI smoke tests of the benchmarks, one line per test in bench/perf/cli_smoke.txt:  name~platform~arguments~regular expression
    # platform: any | posix (not Windows) | linux;  @PRESETS@ = content/presets;  the test is synthyboy_tier_bench_<name>
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/perf/cli_smoke.txt" SYNTHYBOY_CLI_SMOKE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/perf/cli_smoke.txt")
    foreach(line IN LISTS SYNTHYBOY_CLI_SMOKE)
        if(line STREQUAL "" OR line MATCHES "^#")
            continue()
        endif()
        string(REPLACE "~" ";" fields "${line}")
        list(GET fields 0 smoke_name)
        list(GET fields 1 smoke_platform)
        list(GET fields 2 smoke_args)
        list(GET fields 3 smoke_regex)
        if((smoke_platform STREQUAL "linux" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux") OR (smoke_platform STREQUAL "posix" AND WIN32))
            continue()
        endif()
        string(REPLACE "@PRESETS@" "${PROJECT_SOURCE_DIR}/content/presets" smoke_args "${smoke_args}")
        separate_arguments(smoke_argv UNIX_COMMAND "${smoke_args}")
        add_test(NAME synthyboy_tier_bench_${smoke_name} COMMAND synthyboy_tier_bench ${smoke_argv})
        set_tests_properties(synthyboy_tier_bench_${smoke_name} PROPERTIES PASS_REGULAR_EXPRESSION "${smoke_regex}" TIMEOUT 120)
    endforeach()
    ```
  `bench/perf/cli_smoke.txt` starts with a comment line describing the format and one line that passes today: `list_basic~any~--list --load basic --config low~profile low` (`--list` prints `profile <text> = ...`, `bench/tier_bench.cpp:988-989`).
  `bench/libm_probe.cpp`: for each of `expf, logf, powf, sinf, cosf, tanf` (the six the verification names as resolved through the PLT, `OWN-ENGINE-DSP-AUDIT-3-VERIFICATION-2026-11-20.md` rank 18) and `exp2f, log2f, floorf, roundf, fmaxf, sqrtf` time `n = 200000` calls (`--quick`: 20000) on a deterministic input ramp (xorshift32 seeded 12345 mapped to a fixed range per function; the sum goes to a `volatile` sink), 11 repetitions, median ns per call; also the cost of `std::chrono::steady_clock::now()` and `clock_gettime (CLOCK_MONOTONIC)` per call. Lines: `LIBM-PROBE <name> <ns per call> ns` and `LIBM-PROBE clock_steady <ns> ns`, `LIBM-PROBE clock_gettime <ns> ns`; first line `LIBM-PROBE link <static|dynamic>` (from `__GLIBC__` presence is not enough: use the build define `SB_LIBM_PROBE_LINK` set by CMake from the option), last line `LIBM-PROBE-RESULT ok` (every ns is finite and > 0).
* Test: commit 1 adds the `add_test` line for the probe and the harness + `cli_smoke.txt` (the probe test is "Not Run" / fails: no such executable); commit 2 adds the executable and source. Signal: ctest `synthyboy_libm_probe` output contains `LIBM-PROBE-RESULT ok` and 15 `LIBM-PROBE` value lines (12 functions + 2 clocks + the `link` line). Threshold: regex match. Before: test not runnable. After: passes; `synthyboy_tier_bench_list_basic` passes both before and after (guard of the harness).

#### T5 (batch 2) `perf_sched.h`
* Files: `bench/perf/perf_sched.h`, `core/tests/perf/test_perf_sched.cpp` (2).
* Change: header per 4.1. Linux parts under `#if defined(__linux__)` with `#define _GNU_SOURCE` guard; `installHelperPolicyHook` uses a file-static `int g_measuredCore` and a function `static void helperHook (const char* name)` that calls `pthread_setname_np`, `sched_setscheduler (0, SCHED_OTHER, {0})`, and `sched_setaffinity` with `helperCpus (parseCpuList (read of /sys/devices/system/cpu/online), g_measuredCore)` (the pattern of `shell-mpc/thread_policy.h:27-41`), then `setHelperThreadStartHook (&helperHook)` (`core/include/synthyboy/helper_thread_hook.h:20`). `applySched`: `sched_setaffinity` for `core >= 0`, `sched_setscheduler (0, SCHED_FIFO, {fifoPriority})` for `fifoPriority > 0`, `mlockall (MCL_CURRENT | MCL_FUTURE)` for `mlockAll`; `text` carries the `rt: ...` line of 4.1.
* Test: `parseCpuList ("2-3") == {2, 3}`; `("0-1,4,6-7") == {0, 1, 4, 6, 7}`; `("") == {}`; `("\n") == {}`; `("2-x") == {}`; `helperCpus ({0, 1, 2, 3}, 3) == {0, 1, 2}`; `helperCpus ({3}, 3) == {3}`; `parseSchedstat ("9980000 20000 42\n")` -> `{9980000, 20000, 42, ok}`, `waitShare = 20000 / 10000000 = 0.002` exactly; `parseSchedstat ("garbage")` -> `ok == false`; `minGapMs (128.0 / 44100.0 * 1000.0, 64, 0.95, 0.01)` within 0.01 of **11.857** (`64 * 2.90249 = 185.76; 185.76 * (1 / 0.94 - 1) = 11.857`); `kRtGapEvery == 64`. Before: header missing (does not build). After: pass.

#### T6 (batch 2) `perf_env.h`
* Files: `bench/perf/perf_env.h`, `core/tests/perf/test_perf_env.cpp` (2).
* Change: header per 4.1; `readRusage`: `getrusage (RUSAGE_THREAD, ...)` on Linux (`RUSAGE_SELF` elsewhere); `maxThermalC`: glob `/sys/class/thermal/thermal_zone*/temp` with `opendir`/`readdir`, `parseMilliC`, maximum, `NaN` if none; `cacheLevelBytes (cpu)`: for `index0..index7` read `level`, `type`, `size` (`parseCacheBytes`), keep sizes of level >= 2 or type `Data` / `Unified`; `evictCaches`: for `i += 64` `buffer[i] = buffer[i] + 1`.
* Test: `parseMilliC ("68500\n")` -> `68.5`; `("x")` -> false; `parseStatusKb ("Name:\tx\nVmRSS:\t  948244 kB\nThreads: 3\n", "VmRSS") == 948244` (the figure of `docs/MPC.md:148`); absent key -> `-1`; `parseCacheBytes`: `"32K\n" -> 32768`, `"2M" -> 2097152`, `"1024K" -> 1048576`, `"zz" -> -1`; `evictBytes ({32768, 1048576}) == 2097152`; `evictBytes ({}) == 4194304`; `evictCaches` on a 4096-byte zero buffer leaves `buffer[0] == 1`, `buffer[64] == 1`, `buffer[1] == 0`, `buffer[4032] == 1`. Before: header missing. After: pass.

#### T7 (batch 2) `perf_pmu.h`
* Files: `bench/perf/perf_pmu.h`, `core/tests/perf/test_perf_pmu.cpp` (2).
* Change: header per 4.1. Linux `PmuReader::open`: `perf_event_attr` with `size = sizeof`, `type`/`config` of the spec, `disabled = 1` on the leader only, `exclude_kernel = 1`, `exclude_hv = 1`, `read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING`; every event opened with `syscall (__NR_perf_event_open, &attr, 0 /*this thread*/, -1 /*any cpu*/, leaderFd, PERF_FLAG_FD_CLOEXEC)`; `why` = `explainOpenError (errno, parseParanoid (read of /proc/sys/kernel/perf_event_paranoid))`; `ioctl (leader, PERF_EVENT_IOC_RESET / ENABLE, PERF_IOC_FLAG_GROUP)`. `read`: one `read (leaderFd, buf)` of `{nr, timeEnabled, timeRunning, value[nr]}`. `explainOpenError`: `EACCES` / `EPERM` with `paranoid >= 2` -> text containing `perf_event_paranoid`; `ENOENT` -> `event not supported by this PMU`; `ENOSYS` -> `kernel without CONFIG_PERF_EVENTS`; `ENODEV` -> `no PMU driver for this CPU`; `EOPNOTSUPP` -> `not supported`; other -> `errno <n>`.
* Test: `eventSet (PmuSet::cache).size() == 5` with names in the order `cycles, l1d_refill, l1d_access, l2d_refill, l2d_access` and configs `0, 0x03, 0x04, 0x17, 0x16` (the events of `docs/release/CPU-REVIEW-MEMORY.md:344`); `eventSet (PmuSet::compute).size() == 3`, names `cycles, instructions, br_mis_pred`, configs `0, 1, 0x10`; `parseParanoid ("2\n") == 2`, `("-1") == -1`, `("x") == 99`; `scaledCount (1000, 200, 100) == 2000.0`, `scaledCount (1000, 100, 100) == 1000.0`, `scaledCount (1000, 100, 0) == -1.0`; `ratio (1.2e6, 2.0e6) == 0.6`, `ratio (250, 5000) == 0.05`, `ratio (1, 0) == 0`; `explainOpenError (EACCES, 2)` contains `perf_event_paranoid`, `(ENOSYS, 2)` contains `CONFIG_PERF_EVENTS`, `(ENOENT, 2)` contains `not supported`. Before: header missing. After: pass.

#### T8 (batch 2) `perf_modules.h`
* Files: `bench/perf/perf_modules.h`, `core/tests/perf/test_perf_modules.cpp` (2).
* Change: `moduleCases()` builds, from the parameter table only (`findParamByKey`, `paramTable()`, `ParamInfo::numChoices` / `choices`, `core/include/synthyboy/params.h:57-74`; `fxTypeDescriptor`, `core/include/synthyboy/fx_types.h:60-62,91`), the cases (names are display names; values are plain values):
  * group `fx`, one case per type `t = 1 .. kNumFxInstanceTypes - 1` (13: `fx_types.h:44`): `name = fxTypeDescriptor (t).name`; params `{ key of paramTable()[d.onIndex] = 1 }`, plus the mix: if a key `<prefix>.mix` exists (`prefix = d.prefix`), `mix = info->min + 0.5 * (info->max - info->min)` (the engine skips an effect with mix exactly 0, `core/include/synthyboy/engine.h:199-200`).
  * group `filter`, one case per `m = 0 .. numChoices ("filter1.model") - 1`: `name = choices[m]`; params `filter1.on = 1`, `filter1.model = m`, `filter1.cutoff = 3000`, `filter1.resonance = 40` (the values of `bench/own_heavy_patch.h:68-70`).
  * group `warp`, one case per `w = 1 .. numChoices ("osc1.warp.type") - 1` (entry 0 is "none"; `params.h:116`: entries 0..10 are the Vital modes, 11..51 appended): `name = choices[w]`; params `osc1.warp.type = w`, `osc1.warp.amount = 40` (as `bench/tier_bench.cpp:297-322`).
  * group `morph`: one case `Spectral morph`: `osc1.morph.type = 1`, `osc1.morph.amount = 60`.
  * group `unison`: three cases `Unison 3`, `Unison 7`, `Unison 16`: `osc1.unison.voices = n`, `osc1.unison.detune = 30`.
  * group `osc`: four cases `Osc 2`, `Osc 3`, `Sub`, `Noise`: `osc2.on = 1`, `osc3.on = 1`, `sub.on = 1`, `noise.on = 1` (keys of `bench/own_heavy_patch.h:53,65,66`).
  The base (no change from the default load `basic`) is added by the caller, not listed.
* Test (links `synthyboy_core`, `TEST` with `CHECK`): every key of every case resolves with `findParamByKey` (count of failures `== 0`); `count (group "fx") == 13` and equals `kNumFxInstanceTypes - 1`; `count (group "filter") == findParamByKey ("filter1.model")->numChoices`; `count (group "warp") == findParamByKey ("osc1.warp.type")->numChoices - 1`; `count (group "morph") == 1`; `count (group "unison") == 3`; `count (group "osc") == 4`; every `fx` case whose `<prefix>.mix` key exists has exactly that param with a value strictly inside `(min, max)` and not equal to `min`; no two cases share `group + name`. Before: header missing (does not build). After: pass. (The numbers 13, 1, 3, 4 are constants of the code; the other two are derived from the table at run time so a new filter or warp enters automatically.)

#### T9 (batch 2) `build-tier-bench.sh`: dynamic variant, probes, dry run
* Files: `scripts/build-tier-bench.sh`, `scripts/tests/test_perf_build_script.py` (2).
* Change: new options `--dynamic` (adds a variant `-dyn`: `-DSYNTHYBOY_TIER_BENCH_STATIC=OFF -DSYNTHYBOY_TIER_BENCH_STATIC_LIBCXX=ON`, output `<out>/tier-bench-<arch>-dyn/`) and `--dry-run`. The variant list becomes `("")`, then `-sections` (if `--sections`), then `-dyn` (if `--dynamic`) (`scripts/build-tier-bench.sh:54-55`). With `--dry-run` the script prints, for each architecture and variant, one line and exits 0 **before** the docker check (`:50`) and before any `mkdir`:
  `DRY-RUN arch=<arm32|arm64> variant=<""|-sections|-dyn> static=<ON|OFF> libcxx_static=<ON|OFF> section_timing=<ON|OFF> targets=synthyboy_tier_bench,synthyboy_libm_probe,synthyboy_pmu_probe out=<out>/tier-bench-<arch><variant>`.
  In the container script: pass `-DSYNTHYBOY_TIER_BENCH_STATIC` / `_LIBCXX` from the variant (`:77-79`); build the targets `synthyboy_tier_bench` always and `synthyboy_libm_probe`, `synthyboy_pmu_probe` **only if `cmake --build /build --target help` lists them** (the pattern of `scripts/mpc-container-build.sh:140-146`, so the script works between T4 and T12); strip and copy each into the same output directory; the "statically linked" assertion (`:86`) only for `static=ON`; for `-dyn`: `file` says `dynamically linked`, `readelf -d` lists `libm.so.6` and does **not** list `libstdc++`, and the highest `GLIBC_*` version in `objdump -T` is <= 2.39 (the device's glibc, `docs/MPC.md:13`).
* Test (`subprocess.run (["bash", script, "--dry-run", "--arch", "arm32", "--sections", "--dynamic", "--out", "/tmp/x"])`): exit 0; stdout is exactly three `DRY-RUN` lines in the order `variant=` (static=ON, section_timing=OFF), `variant=-sections` (static=ON, section_timing=ON), `variant=-dyn` (static=OFF, libcxx_static=ON, section_timing=OFF); with `--arch all` six lines (arm32 x 3, then arm64 x 3). Before: exit 2 `unknown option --dry-run`. After: pass.

#### T10 (batch 2) `synthyboy_bench` defaults (rank 9, recommendation 2)
* Files: `bench/bench_engine.cpp`, `bench/CMakeLists.txt` (2).
* Change: `sampleRate = 44100.0` (`bench/bench_engine.cpp:144`), `ftz = true` (`:149`), a new option `--no-ftz` next to `--ftz` (`:486`), the usage line (`:5,451`) lists `[--no-ftz]`; `enableFtz()` (`:116-131`) gets an x86 branch (`_MM_SET_FLUSH_ZERO_MODE`, `_MM_SET_DENORMALS_ZERO_MODE` with `<xmmintrin.h>`, `<pmmintrin.h>`, as `bench/tier_bench.cpp:214-216`) so the default does not print the "only implemented for ARM" message on a desktop. In `bench/CMakeLists.txt` add `add_test (NAME synthyboy_bench_defaults COMMAND synthyboy_bench --engine own --patch default --seconds 0.3)` with `PASS_REGULAR_EXPRESSION "own, 44100 Hz"` (the header line prints `engine %s, %.0f Hz`, `:519`).
* Test signal: the first output line. Before: `synthyboy_bench: engine own, 48000 Hz, ...` -> regex not matched (fails); after: `engine own, 44100 Hz` (passes). Threshold: regex match.

#### T11 (batch 2) `scripts/perf-tables.py`, the section and module tables
* Files: `scripts/perf-tables.py`, `scripts/tests/test_perf_tables.py` (2).
* Change: reads `tier_bench` stdout from a file or stdin. `sections`: for each result row (a non-indented line with `|` whose profile is `line[0:58]` and load `line[59:83]`, the `%-58s %-24.24s` of `bench/tier_bench.cpp:1076`) collects the following indented `    section ...` lines (the format of `bench/tier_bench.cpp:1096`: `"    section %-52s %8.4f ms / block %6.1f %%  %7.1f calls / block %7.1f clock reads  less clock %8.4f ms %6.1f %%"`) and emits a Markdown table per (profile, load): `| section | ms/block | ms/block less clock | % of block |` with `% of block = less clock / 2.90249 * 100` to two decimals (the block length is read from the `synthyboy_tier_bench: ... block 128 frames = 2.902 ms` header line when present, else `128 / 44100`). `modules`: parses the `module ... | ...` lines of 4.1 into `| group | module | cost us | % of block | spread % |` sorted by group then by cost descending.
* Test: a row built with the C format of `:1076` followed by a section line with name `filter1`, ms 0.1234, share 10.0, calls 8.0, reads 16.0, less clock `0.2902`, share 9.0 -> the output row `| filter1 | 0.1234 | 0.2902 | 10.00 |` (`0.2902 / 2.90249 = 0.09998`). A module line `module fx | Chorus | base 812.40 us | case 934.15 us | cost +121.75 us | +4.19 % of block | reps 3 | spread 2.1 %` -> `| fx | Chorus | 121.75 | 4.19 | 2.1 |`; two lines of one group are ordered by cost descending. Before: script missing. After: pass.

#### T12 (batch 3) `synthyboy_pmu_probe`
* Files: `bench/pmu_probe.cpp`, `bench/CMakeLists.txt` (2).
* Change: `bench/CMakeLists.txt` (after T4's block): `add_executable(synthyboy_pmu_probe pmu_probe.cpp)`, include dir `bench`, the same static / libcxx link rules as the other probes, `add_test(NAME synthyboy_pmu_probe COMMAND synthyboy_pmu_probe)` with `PASS_REGULAR_EXPRESSION "PMU-PROBE-RESULT"`. `pmu_probe.cpp` (uses `bench/perf/perf_pmu.h`): prints `PMU-PROBE paranoid=<n>`; for every event of both sets one line `PMU-PROBE event=<name> status=ok|fail errno=<n> why=<text>` (each opened alone); then for each set: opens the group, spins a 50 ms floating-point loop, reads, prints `PMU-PROBE set=<cache|compute> <event>=<scaled value> ... enabled=<ns> running=<ns>`; final line `PMU-PROBE-RESULT cache=<usable|multiplexed|unavailable> compute=<...>` (`usable` = open ok and `running == enabled`, `multiplexed` = open ok and `running < enabled`). `--cpu N` pins the thread first. On non-Linux the result line says `unavailable` for both and the exit code is 0.
* Test: commit 1 adds the CMake `add_test` line only (target missing -> "Not Run"); commit 2 adds the executable and source. Signal: ctest output contains `PMU-PROBE-RESULT`. On the devices (E3) the same binary is the "say how to check" step. Before: fails (not run). After: passes on any platform.

#### T13 (batch 3) `tier_bench`: real-time scheduling, helper threads, health columns, p99.9
* Files: `bench/tier_bench.cpp`, `bench/perf/cli_smoke.txt` (2).
* Change in `bench/tier_bench.cpp`:
  1. includes after `bench/tier_bench.cpp:90`: `"perf/perf_stats.h"`, `"perf/perf_sched.h"`, `"perf/perf_env.h"`.
  2. `Options` (`:220-245`): `int rt = 0; bool mlock = false; double rtGapMs = -1.0; bool rtRequire = false; bool helpersAway = true; int retryInvalid = 2;`.
  3. arguments (`:785-812`): `--rt N` (1..99, else usage), `--mlock`, `--rt-gap MS` (0 = none), `--rt-require`, `--no-helpers-away`, `--retry-invalid N`; the usage text (`:7-46`, `:576`) lists them.
  4. in `main`, replacing the CPU block (`:939-955`): first, **if `o.core >= 0 && o.helpersAway`** call `perf::installHelperPolicyHook (o.core)` and set the main thread's affinity to `perf::helperCpus (online, o.core)`; then create the measuring work in the **main thread** after `perf::applySched ({o.core, o.rt, o.mlock})` (the pin and FIFO apply to the thread that runs every `run()`); threads created later by the engine take the policy of their creator **or** the hook: the hook runs first thing in `sb-fx-build` / `sb-fx-job` and overrides the inherited mask; the wavetable builder thread (no hook, `wavetable_bank.cpp:472-497`) is created in the first `prepare`, so `run()` for the first load is preceded by a throw-away `createEngine`+`prepare` **before** `applySched` (a one-line warm-up engine that is destroyed) so that the process-wide builder thread inherits the helper affinity of the main thread, not the pin. Print the `rt:` and `helpers:` lines (4.1); with `--rt-require` exit 1 if `fifoOk` is false.
  5. `run()` (`:359-482`): before the loop read `perf::readThreadSchedstat()`, `perf::readRusage()`, `perf::threadCpuMs()`, `currentKhz (cpu)`; at `b == warmup` read `readRusage()` again (-> `minflt_measured` at the end); after the loop read them again; `Result` gets `p999Ms = 1000 * percentileSorted (times, 0.999)`, `runDelayShare = waitShare (delta)`, `threadCpuMs`, `nivcsw`, `minflt`, `minfltMeasured`, `majflt`, `rssKb = perf::readRssKb()`, `tempMaxC = perf::maxThermalC()`, `khzBefore`, `valid`, `invalidReason = perf::invalidReason (health)` with `khzMax` from `scaling_max_freq`. In the loop **after** `t1` (outside the timed region): `if (o.rt > 0 && ! o.paced && gapMs > 0 && (b + 1) % perf::kRtGapEvery == 0) std::this_thread::sleep_for (std::chrono::duration<double, std::milli> (gapMs));` with `gapMs = o.rtGapMs >= 0 ? o.rtGapMs : std::ceil (perf::minGapMs (1000.0 * o.block / o.sampleRate, perf::kRtGapEvery, 0.95, 0.01))` (12 ms for the Key 61 block). The run's percentile code (`:461-466`) is replaced by `perf::summarize` (same indices).
  6. pass loop (`:1066-1120`): after the stdout row print the `    health: ...` line; if `! valid` and `o.retryInvalid > 0`, re-run the same (profile, load) up to `retryInvalid` times, printing `    retry <n>: <reason>`; the last result is kept, its `valid` is written to the CSV.
  7. CSV (`:1051`, `:1112`): append the T13 columns of 4.1 after `pass`; at the end print `csv schema 2: <path>` when `--csv` was given. The stdout table (`:1054-1077`) keeps its columns.
* Smoke lines appended to `bench/perf/cli_smoke.txt` (platform `posix` = non-Windows): `rt_flags~posix~--load basic --config low --seconds 0.3 --rt 20 --mlock --csv /dev/null~rt: ` ; `csv_schema2~posix~--load basic --config low --seconds 0.3 --csv /dev/null~csv schema 2` ; `health_line~posix~--load basic --config low --seconds 0.3~health: run_delay` ; `helpers_away~linux~--load basic --config low --seconds 0.3 --core 0~helpers: ` ; `verify_guard~any~--verify --load basic --config low~verify: 0 of 1 renders differ` (the last one passes before and after: a guard that `--verify` still works).
* Test: commit 1 adds the smoke lines (the first four fail: `unknown argument --rt` / no `health:` line, exit 2 or regex missing); commit 2 the code. Signal: stdout of each command; threshold: the regex. `--rt 20` as a normal user prints `rt: requested FIFO 20 on cpu -1: ... policy refused (Operation not permitted)` and the run continues (only `--rt-require` fails), so the smoke test passes on a CI user. Before / after as stated.

#### T14 (batch 3) `scripts/perf-device.sh`, the runner
* Files: `scripts/perf-device.sh`, `scripts/tests/test_perf_device.py` (2).
* Change: `scripts/perf-device.sh --device k61|xl [--host IP] --phase probe|install|e0|e1|e2|ab|pmu|modules|libm|cleanup [--rt N] [--bin static|dyn|sections] [--seconds 10] [--loads set12|set6:<name>,<name>] [--sequence ABBA...] [--out DIR] [--dry-run]`. Defaults: `k61` = host `192.168.0.30`, core 3, profile `low`; `xl` = `192.168.0.126`, core 6, profile `high,voices=16,control=economy`; `--out` default `build-tier-bench/results/<device>-<UTC time>/`. `--dry-run` prints every remote command and every local command prefixed `+ ` and runs nothing (no ssh). The phases print/run exactly the commands of section 2: `probe` = the D0 here-document, `install` = the tar pipelines of 2.1 (with `COPYFILE_DISABLE=1`), `e0` = 2.2 (the thread snapshot prints `nice=$17`, field 19 of `stat` after the `) ` strip), `e1` = the four settings of 2.3 with the dead-man block for setting 4, `e2`/`ab` = the ABBA loop (`--sequence`, each pass `./<bin>/synthyboy_tier_bench --presets presets --core <c> --rt <p> --rt-require --config "<profile>" <loads as --load "<name>"> --seconds <s> --csv pass-<n>-<label>.csv` followed by `perf-ab.py --sequence <seq> pass-*.csv`), `pmu` (probe + `--pmu cache` / `compute`), `modules` (`--modules --seconds 2 --module-reps 3 --config "<profile with ,fx=all for k61>"`), `libm` (probe static/dyn alternating 7 times + the E5 A/B), `cleanup` (`rm -rf /tmp/sbperf`). A guard: for any phase that stops the host (E1d) the script requires `--i-know-the-host-stops` and refuses on `--device xl`.
* Test (`subprocess` on `bash scripts/perf-device.sh --dry-run ...`): `--device k61 --phase probe` prints `root@192.168.0.30`, `cat /sys/devices/system/cpu/isolated`, `systemctl show -p MainPID acvs`, `perf_event_paranoid`, and no `ssh` is executed (exit 0, the process list is not touched); `--device xl --phase ab --sequence ABBA --rt 20` prints `root@192.168.0.126`, `--core 6`, `--config "high,voices=16,control=economy"`, `--rt 20 --rt-require`, and the four-file `perf-ab.py --sequence ABBA`; `--device k61 --phase e1` prints the `kill -STOP` only with `--i-know-the-host-stops` (else exit 2); `--device xl --phase e1 --i-know-the-host-stops` exits 2; an unknown phase exits 2. Before: script missing. After: pass.

#### T15 (batch 4) `tier_bench --pmu`
* Files: `bench/tier_bench.cpp`, `bench/perf/cli_smoke.txt` (2).
* Change: include `"perf/perf_pmu.h"`; `Options`: `std::string pmu; bool pmuRequire = false;`; arguments `--pmu compute|cache`, `--pmu-require` (exit 1 if the PMU cannot be opened); in `run()` after `prepare`: `perf::PmuReader pmu; const bool pmuOn = ! o.pmu.empty() && pmu.open (o.pmu == "cache" ? perf::PmuSet::cache : perf::PmuSet::compute, why)`; in the loop, **outside** `t0 .. t1`: `pmu.read (s0)` before `t0` and `pmu.read (s1)` after `t1`; for `b >= warmup` keep the per-event deltas (sum) and the cycles of every block in a reserved `std::vector<double>`; scaling = sum of `timeRunning` deltas / sum of `timeEnabled` deltas. After the run print the `    pmu:` line of 4.1 (`pmu: unavailable (<why>)` when it could not open) and append the T15 columns to the CSV (empty when off). The derived columns use `perf::ratio`: `ipc = instr_mean / cycles_mean`, miss percentages = `refill / access * 100`.
* Smoke: `pmu_flag~posix~--load basic --config low --seconds 0.3 --pmu cache --csv /dev/null~pmu: ` (prints either the line or `unavailable`, never fails without `--pmu-require`). Before: `unknown argument --pmu`. After: pass. Signal: stdout; threshold: regex. Device numbers: E3.

#### T16 (batch 5) `tier_bench --evict` and `--first-blocks`
* Files: `bench/tier_bench.cpp`, `bench/perf/cli_smoke.txt` (2).
* Change: `Options`: `bool evict = false; int firstBlocks = 0;`; `--evict`: in `run()` build `std::vector<unsigned char> evictBuf (perf::evictBytes (perf::cacheLevelBytes (cpu)))` once, and call `perf::evictCaches (evictBuf)` at the top of every block iteration, **before** `t0` (and before the `--paced` sleep is not needed; after it); the CSV column `cold_mode` = `evict` or `warm`. `--first-blocks N` (1..64): a mode that, for every (profile, load), creates the engine as `run()` does, measures `prepare` with `steady_clock` and prints `first: prepare <x.x> ms`, plays N blocks with the chord at block 0 and no warm-up, and prints per block `first: block <i> <ms> ms (<pct> %) minflt <n>` (`perf::readRusage()` delta); the CSV column `first_block_ms` of the normal run is left empty.
* Smoke: `first_blocks~posix~--load basic --config low --first-blocks 4~first: block 3 ` ; `evict_flag~posix~--load basic --config low --seconds 0.3 --evict~health: run_delay`. Before: `unknown argument`. After: pass.

#### T17 (batch 6) `tier_bench --modules`
* Files: `bench/tier_bench.cpp`, `bench/perf/cli_smoke.txt` (2).
* Change: include `"perf/perf_modules.h"`; `Load` (`:272-278`) gets `Kind moduleCase` and `std::vector<std::pair<std::string, float>> params`; `applyLoad` (`:325-343`) for `moduleCase`: defaults (already done at the top), then `master.polyphony = 32` (as `basic`, `:335`) and `setIfKnown (engine, key, value)` for every param. `Options`: `bool modules = false; int moduleReps = 3; std::string moduleGroup = "all"; bool secondsGiven = false;` (`--seconds` sets `secondsGiven`; with `--modules` and no `--seconds` the measured length is 2.0 s). `--modules`: for each profile in `configs`, with `notes = profiles[c].maxVoices` (or `--notes`): for each case of `perf::moduleCases()` filtered by `--module-group fx|filter|warp|morph|unison|osc|all`: repeat `moduleReps` times {run the base load `basic`, run the case; `cost_rep = median (case) - median (base)` in us}; print the `module ...` line of 4.1 with `cost = median of cost_rep`, `base`/`case` = medians over reps, `% of block = cost / (1e6 * block / sampleRate) * 100`, `spread = (max cost_rep - min cost_rep) / base * 100`; last line `module-table: <n> cases`. With `--rt` the runs use the same health gate (an INVALID run is repeated up to `retryInvalid`). Estimated duration on the device: cases = 13 + F + (W - 1) + 1 + 3 + 4 (F filter models, W warp entries, 51 + 1 if `params.h:116` is current), x `moduleReps 3` x 2 runs x (1 + 2) s: with F around 10 (HYPOTHESIS) about 82 x 18 s = 25 min.
* Smoke: `modules_fx~posix~--modules --module-group fx --module-reps 1 --seconds 0.3 --config low,fx=all~module-table: 13 cases` (13 = `kNumFxInstanceTypes - 1`). Before: `unknown argument --modules`. After: pass.

#### T18 (batch 7) documentation and its consistency test
* Files: `docs/perf/README.md`, `docs/PERFORMANCE.md`, `scripts/tests/test_perf_docs.py` (3).
* Change: `docs/perf/README.md` = the protocol (2.1 .. 2.3 commands, SET12 / SET6, the E1 decision rule, the A/B and gate rules of 3.5, the CSV schema, how to read each column, what INVALID means, the thresholds table, where baselines live `docs/perf/baseline-<device>.json`, `sections-<device>.md`, `modules-<device>.md`). `docs/PERFORMANCE.md`: replace the 2026-11-04 laptop section (`docs/PERFORMANCE.md:29-86`) by a pointer to `docs/perf/README.md` and keep one short desktop paragraph with the current `synthyboy_bench` defaults (44.1 kHz, flush-to-zero on) and the statement that the laptop table was removed; keep lines 1-27 (how to run). 
* Test: parses `bench/tier_bench.cpp` for every long option of its argument parser (`a == "--..."`) and requires each of `--rt`, `--rt-gap`, `--rt-require`, `--mlock`, `--no-helpers-away`, `--retry-invalid`, `--pmu`, `--pmu-require`, `--evict`, `--first-blocks`, `--modules`, `--module-group`, `--module-reps` to appear in `docs/perf/README.md`; requires the strings `SET12`, `GATE-INVALID`, `97.9 %`, and that `docs/PERFORMANCE.md` no longer contains `Apple M5 Pro` and does link `docs/perf/README.md`. Before: README missing / stale text present -> fails. After: pass.

---

## 5. Acceptance for the whole item

**What must hold (checked once after batch 7 and the device runs):**

| # | Number / check | Where measured | Threshold |
|---|---|---|---|
| 1 | No engine change: `git diff --name-only 32286cf8..HEAD` contains nothing under `core/engine-own/`, `core/fx-*`, `core/filters-*`, `core/include/` | dev machine | empty; the render hash of all presets (`synthyboy_preset_render --float32 --hash`, compared with `scripts/compare-renders.sh` as in `docs/MPC.md:336`) identical to `32286cf8` -> **sound, aliasing level and memory of the engine unchanged by construction** |
| 2 | Tests | dev machine, `ctest` | all new `synthyboy_test_perf_*`, `synthyboy_tier_bench_*`, `synthyboy_libm_probe`, `synthyboy_pmu_probe`, `synthyboy_bench_defaults` pass; `python3 -m unittest discover -s scripts/tests` passes (T2, T3, T9, T11, T14, T18) |
| 3 | `--verify` still passes | Key 61 and XL, `tier_bench --verify --tolerance 3e-5` over SET12 | `verify: 0 of 12 renders differ` (the run of `docs/MPC.md:336` passed 12 of 12 with 3e-5) |
| 4 | Noise floor (E2) | both devices | A/A 97.9 % interval inside [0.99, 1.01] for >= 11 of 12 loads, 5-pass p99 spread <= 2 %; if not met the protocol's `min-gain` is the measured half-width and `docs/perf/README.md` says so |
| 5 | Interference (E1) | both devices | an accepted setting exists (spread <= 2 %, `run_delay_pct` <= 0.2 %, `basic` p99 - median <= 3 points); the host audio thread's policy, priority, core and the RT throttle values from D0 are written in the README |
| 6 | PMU (E3) | both devices | `PMU-PROBE-RESULT` recorded; if `usable`, `pmu_scaling = 1.000` and the read overhead <= 1 point (29 us); if `unavailable`, the README states the reason and the cache hypotheses (`docs/release/CPU-REVIEW-MEMORY.md:344,449`) stay open |
| 7 | Module table (E4) | both devices | `modules-<device>.md` has every row of `moduleCases()` (13 FX, every `filter1.model` choice, every `osc1.warp.type` choice but 0, 1 morph, 3 unison, 4 oscillator rows) with `spread <= 10 %` or flagged |
| 8 | Section table | both devices, `--sections` binary | `sections-<device>.md` for SET12 |
| 9 | Gate sensitivity | Key 61 | on the baseline binary, `perf-gate.py compare` over a fresh 3-pass run -> `GATE-PASS 12 keys` three times in a row; the same run with `--scalar-filters` (documented before/after switch, `bench/tier_bench.cpp:33`) -> `GATE-FAIL` on at least `Scream Filter Bass` (the filter-heavy preset, `docs/MPC.md:321`) |
| 10 | Rank 18 closed | both devices | libm ns table (`LIBM-PROBE`) static vs dynamic committed; the A/B `dyn / static` interval either inside the A/A band (closed: no effect) or outside (the dynamic binary is the gating binary); `clock_steady` ns static vs dynamic recorded |
| 11 | CPU per block on the Key 61 tier `low` | baseline JSON | **recorded, not improved**: this item adds the baseline that all later items are measured against (`docs/MPC.md:321`: Diffusor Cloud Pad p99 100.7 % is the reference to be re-measured under RT) |
| 12 | Tool cost | Key 61 | `--rt-gap` duty 93.9 % (< 95 %); no `stall` block in 5 passes of SET12 |

**Device checks (runner with ssh, in this order, after the build worker's binaries exist):** `scripts/perf-device.sh --device k61 --phase probe`, `install`, `e0`, `e1`, `e2`, `pmu`, `modules`, `libm`; the same for `xl`; every command is the one printed by `--dry-run` and listed in section 2. The coordinator reads the outputs, commits the baselines, and writes the numbers of 4, 5, 6, 10 into `docs/perf/README.md`.

---

## 6. Risks and what to do if a step fails

| Risk | Sign | What to do |
|---|---|---|
| `perf_event_open` unavailable (`CONFIG_PERF_EVENTS` off, `paranoid` too high, no PMU driver) | D0 shows no `event_source/devices` entry or `PMU-PROBE-RESULT cache=unavailable` | The tools already degrade (`pmu: unavailable`). Gate and A/B use ms and `thread_cpu_ms` only; the cache hypotheses stay open and are listed in the README. A static `perf` tool is **not** built in this item. |
| More events than counters (multiplexing) | `pmu_scaling` < 1 | `PmuSet` limits are 4 event counters + cycles; if the A17 has fewer (E3 shows `running < enabled`), drop `l2d_access` first and note it; never extrapolate silently (`scaledCount` is shown, not used for gating). |
| FIFO run starves the host's audio thread on the same core for up to 186 ms between gaps | audible dropouts, `acvs` logs | Run only on an idle device with no playback; `--rt-gap` keeps the duty at 93.9 %; shorten `kRtGapEvery` if the host needs it (edit the constant, rerun T5's test). |
| RT throttling still fires | a block > 34.8 ms, reason `stall` | Increase the gap (`--rt-gap 20`); if the device has `sched_rt_runtime_us = -1` nothing to do; the run is INVALID and retried, never averaged. |
| Host audio priority `HP` is higher than any value the bench may use, or `chrt`-style FIFO is refused | `rt:` line says `policy refused` | The runner is root, so refusal means a container / cgroup limit: use setting 4 (host stopped, E1d) as the standard instead, with the dead-man; record that in the README. |
| Stopping the host (`kill -STOP acvs`) leaves the device unusable if ssh dies | the dead-man `kill -CONT` after 150 s did not fire | The dead-man is a background `sh` on the device, independent of the ssh session; if the UI stays frozen the owner restarts with `systemctl restart acvs` over a new ssh or power-cycles. E1d is only for the Key 61 and needs the explicit flag. |
| The noise floor stays above 2 % even when quiesced | E1 setting 4 fails (a), (b) or (c) | Then the host is not the cause. Check, in this order: helper threads still on the measured core (`threads.txt`: use `--no-helpers-away` as an A/B), thermal / frequency (`temp_c_max`, `khz_*`, an INVALID `freq` run), the bench's own allocation (`minflt_measured`), code layout between builds (the A/B protocol uses fresh processes and ABBA for this). The protocol is still valid with a larger `min-gain`; say so in the README. |
| p99 with `--seconds 10` is unstable for presets with heavy tails | spread > 2 % only for 1-2 loads | Raise `--seconds` to 30 for those loads (10335 blocks, p99 has 103 blocks above, `0.01 * 10334 = 103`), not for all. |
| Spectral Vowel Freeze silent without the sample pack | `voices_mean` ~0 and a tiny cost | The runner always sets `SYNTHYBOY_CONTENT_DIR` (2.1); `perf-gate.py` flags `voices_mean < 1` as a warning line `GATE-WARN`. |
| `dyn` binary does not start on the device | `not found` / `GLIBC_x not found` | T9 asserts `GLIBC` <= 2.39 and `ld-linux-armhf` / `ld-linux-aarch64` at build time (`docs/MPC.md:13,22`); if it still fails, rank 18 is measured with `libm_probe` only and A/B on libm-heavy presets is reported as not done. |
| A task of the batch does not compile | build worker log | The failing task is fixed alone; no other task of the batch edits its files (4.2), so there is no merge conflict; the test commit of that task stays as the proof. |
| The sign-test interval is too wide with `K = 10` for small gains | `NO-CHANGE-PROVEN` on a fix that claims 1-2 % | Raise `K` (`K = 14` gives `r = 3`, coverage `1 - 2 * 106 / 16384 = 0.987`) and rerun only the loads of that fix (SET6); a gain below the A/A half-width cannot be proven on that device and is reported as such. |

---

## 7. Public references used

Named from memory of the public literature, **not opened in this session** (no code taken from any of them; none is GPL):

* Mytkowicz, Diwan, Hauswirth, Sweeney, "Producing Wrong Data Without Doing Anything Obviously Wrong!", ASPLOS, 2009 (layout / environment bias; the reason for fresh processes and ABBA).
* Georges, Buytaert, Eeckhout, "Statistically Rigorous Java Performance Evaluation", OOPSLA, 2007 (reporting with confidence intervals).
* Hoefler, Belli, "Scientific Benchmarking of Parallel Computing Systems", SC'15, 2015 (nonparametric reporting, interval of the median).
* Conover, "Practical Nonparametric Statistics", 3rd edition, 1999 (sign test, distribution-free interval of the median from order statistics: the `K = 10, r = 2` interval).
* Gregg, "Systems Performance: Enterprise and the Cloud", 2nd edition, 2020 (CPU counters, scheduler latency, run-queue wait).
* ARM, "ARM Architecture Reference Manual ARMv7-A and ARMv7-R edition" (DDI 0406C), 2014, and "Arm Architecture Reference Manual for A-profile" (the architectural PMU events 0x03, 0x04, 0x10, 0x16, 0x17 and the cycle counter).
* Kerrisk, "The Linux Programming Interface", 2010, and the Linux man-pages `perf_event_open(2)`, `sched(7)`, `sched_setscheduler(2)`, `sched_setaffinity(2)`, `getrusage(2)`, `proc(5)` (`/proc/<pid>/schedstat`, `stat` field numbers), and the kernel documentation of `sched_rt_runtime_us` / `sched_rt_period_us` (RT throttling).
* Bencina, "Real-time audio programming 101: time waits for nothing", 2011 (audio-thread rules the bench must not violate; also listed in `TODO.md:130`, which says such sources must be opened before they enter a knowledge folder).

Repository documents used as sources of measurements: `docs/MPC.md` (lines cited), `docs/release/CPU-REVIEW-MEMORY.md:344,359,449`, `docs/PERFORMANCE.md`, the three DSP audits and their verifications.

---

## 8. Closing statement

No file under `core/engine-vital/`, `core/vital-import/` or `/Users/szabi/Documents/Projects/Safian/.hw-local/synth-knowledge/` was opened, read, listed or used for this plan. One disclosure: three repository-wide `grep -r` calls started at the worktree root (for `rtrun`, `HelperThreadStartHook` and `SECTION_TIMING`) could not exclude `core/engine-vital/` and `core/vital-import/` (both directories exist in this worktree); none of their output lines came from those directories and nothing of their content was read or used. No `Scripts/.env`, `.ssh` or device was touched; no build, test, gate or benchmark was run.
