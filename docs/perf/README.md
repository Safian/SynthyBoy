# Performance Measurement Protocol and Benchmarks

This directory contains the measurement protocol, baseline data, and reports for SynthyBoy performance benchmarks on target embedded devices (Akai MPC Key 61, Akai MPC XL) and desktop environments, as specified in `docs/plans/perf-measurement.md`.

## 1. Overview & Directory Contents

Performance results and regression baselines are version-controlled here:
* `docs/perf/baseline-<device>.json`: Accepted 5-pass baseline results for automated regression gating on a device (e.g. `docs/perf/baseline-k61.json`, `docs/perf/baseline-xl.json`).
* `docs/perf/sections-<device>.md`: Per-section DSP timing breakdown tables produced from `synthyboy_tier_bench --sections` (e.g. `sections-k61.md`, `sections-xl.md`).
* `docs/perf/modules-<device>.md`: Catalog of standalone module costs produced from `synthyboy_tier_bench --modules` (e.g. `modules-k61.md`, `modules-xl.md`).

---

## 2. Tools & Scripts

The hardware counter probe:

* **`synthyboy_pmu_probe`**: Standalone hardware counter probe. Inspects `perf_event_paranoid`, tests `perf_event_open` for architectural counter groups (`compute` and `cache`), and outputs `PMU-PROBE-RESULT usable|multiplexed|unavailable`.

---

## 3. Device Protocol & Phases (Sections 2.1 .. 2.3)

### 3.1 Device Preparation & Read-Only Probes (D0, D1)

**D0 read-only probe**:
Gathers kernel model, online/isolated CPUs, frequency governor, thermal status, RT throttle parameters, PMU capabilities, and host `acvs` audio thread metadata:
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

#### Probe Decisions & Hypotheses Table

| Probe line | Decides | Expected / HYPOTHESIS |
|---|---|---|
| `isolated`, `Cpus_allowed_list`, `cpu=` of `AudioWorker*` | Which core(s) carry the host's audio threads; whether core 3 (Key 61) / 6 (XL) is one of them | Key 61 isolated `2-3` is documented (`docs/MPC.md:147`); which isolated core hosts an audio thread: HYPOTHESIS core 3 (`:321`). |
| `rtprio=`, `policy=` of `AudioWorker*` | The real priority `HP` of the host's audio threads (replaces the audit's initial estimate "20") | HYPOTHESIS `policy=1` (FIFO), `rtprio` discovered via probe. |
| `runtime_us / period_us` | RT throttling: default 950000 / 1000000 stalls RT tasks for 50 ms after 950 ms runtime | HYPOTHESIS default; enforces `--rt-gap 12` ms every 64 blocks. |
| `paranoid`, `event_source/devices`, `CONFIG_PERF_EVENTS`, dmesg | Whether `perf_event_open` works for hardware performance counters | HYPOTHESIS line like `hw perfevents: enabled with armv7_cortex_a17 PMU driver, N counters available`. |
| `CONFIG_SCHEDSTATS` / `SCHED_INFO` | Whether `/proc/<tid>/schedstat` exists for run-delay contamination detection | HYPOTHESIS present; if missing, falls back to `nivcsw` (`getrusage`). |
| cache sizes | Eviction buffer size (`2 x` largest cache level) | HYPOTHESIS Cortex-A17 L2 of 1 or 2 MiB (fallback 4 MiB). |
| libm paths | Whether host maps `libm.so.6` of glibc 2.39 (`docs/MPC.md:13`) | Dynamic build matches host dynamic libraries. |

---

### 3.2 Phase E0: Current Baseline & Contamination Check

Runs today's binary for 5 passes with `--repeat 5` and checks thread scheduling affinities (`threads.txt`).
Confirms contamination when:
- 5-pass p99 spread of `basic` and `Diffusor Cloud Pad` > 2 %.
- p99 - median gap of `basic` >= 15 points.
- Helper threads inherit affinity to core 3.
- `run_delay_pct` (`wait_ns / (run_ns + wait_ns)`) > 0.2 %.

---

### 3.3 Phase E1: Interference Characterisation & Decision Rule

Evaluates four scheduling configurations (where `HP` is the host priority determined in D0):
1. `--core 3` (default scheduling)
2. `--core 3 --rt $HP --rt-require`
3. `--core 3 --rt $((HP+1)) --rt-require`
4. Setting 2 with the host stopped via dead-man watchdog (`kill -STOP $P` and background `(sleep 150; kill -CONT $P)`). Note: Never run host stop on the XL while a project is open.

#### E1 Decision Rule
Evaluated least-intrusive first (2 -> 3 -> 4). A setting is **accepted** when for both loads (`basic` and `Diffusor Cloud Pad`):
1. 5-pass p99 spread <= 2 %
2. Median over passes of `run_delay_pct` <= 0.2 %
3. `p99 - median` gap of `basic` <= 3 points.

If setting 4 fails, interference is not caused by the host: investigate engine helper threads (`--no-helpers-away`), thermal throttling (`temp_c_max`), or DVFS frequency changes (`khz_before`, `khz_after`).

---

### 3.4 Phase E2: Noise Floor (A/A Testing)

Validates the test bench reproducibility using two copies of the same binary (`A` and `B`), across SET12, using an `ABBA` sequence of 20 passes (10 pairs).
- **Protocol acceptance rule**: Accepted when the **97.9 %** confidence interval of the paired ratio lies inside `[0.99, 1.01]` for at least 11 of the 12 loads.
- If this noise floor target is not met, `min-gain` is adjusted to the measured half-width.

---

### 3.5 Phase E3: PMU Counter Validation

- Executes `synthyboy_pmu_probe` on the device.
- Runs `tier_bench --pmu cache` and `tier_bench --pmu compute` on `Diffusor Cloud Pad`.
- Valid when `pmu_scaling == 1.000` (no multiplexing).
- Read overhead check: median block time with and without `--pmu` must differ by <= 1 point (29 us).
- Drift diagnosis: if block time drifts across days but `cycles_mean` does not, the cause is CPU frequency / DVFS; if both drift, memory layout or caching changed.

---

### 3.6 Phase E4: Differential Module Cost Catalog

Runs `synthyboy_tier_bench --modules` (or with `--module-group fx|filter|warp|morph|unison|osc`) across profiles.
Measures standalone component costs without nested in-engine timers:
$$\text{cost} = \text{median}(\text{case}) - \text{median}(\text{base})$$
where `base` is the default patch (`basic`) and `case` enables only the single targeted module.

---

### 3.7 Phase E5: Libm Probe & Dynamic Link Verification

- Runs `synthyboy_libm_probe` comparing static vs dynamic libm across 7 alternating repetitions.
- Executes A/B testing of static vs dynamic builds on `worst`, `Bode Barber Riser`, and `Hyper Saw Lead` with `low,fx=all`.
- Decision rule: If the 97.9 % interval of ratio `dyn / static` lies inside the E2 A/A band for all three loads, rank 18 is closed as having no measurable difference and the static binary remains default; otherwise, the dynamic binary becomes the gating binary.

---

## 4. Load Sets & Syntax

### Standard Load Sets
* **SET12**: Comprehensive 12-load benchmark suite covering synthetic extremes and factory presets across pad, bass, lead, and acoustic/vocal types:
  `worst`, `heavy`, `basic`, `Diffusor Cloud Pad`, `Hyper Saw Lead`, `Bode Barber Riser`, `Plucked Air Bass`, `FM Growl Bass`, `Vowel Motion Choir`, `Analog Drift Brass`, `Crystal Bell Arp`, `Subtle Tape Keys`.
* **SET6**: Rapid 6-load evaluation suite consisting of `basic`, `heavy`, and 4 representative factory presets.

### Load Argument Syntax (`--load`)
* `worst`: Synthetic worst case (3 osc x 16 unison, morph, warp, 9 effects).
* `heavy`: Legacy reference patch (`bench/own_heavy_patch.h`: 3 x 7 unison, morph, warp).
* `basic`: Table default patch (1 saw oscillator, 1 digital filter, 32 voices).
* **`dist:<type>`**: Distortion benchmark load. Starts from `basic` and enables distortion module of type `<type>` (0..12), drive +18 dB, mix 100%.
* **`warp:<type>:<amount>`**: Oscillator warp benchmark load. Starts from `basic` with osc 1 warp of type `<type>`, amount `<amount>`, and 3 unison voices.
* `<preset name or path>`: Any factory preset loaded from `.sbpreset` file.

---

## 5. Interleaved A/B Protocol & Automated Regression Gate (Section 3.5)

### Key 61 note-on comparison

Run the private `.hw-local/mpc-perf/noteon-burst/run-noteon-burst.sh LABEL BASELINE_BINARY CANDIDATE_BINARY` with both prebuilt arm32 binaries. The runner stages content once, holds one device lock and runs baseline, candidate, then baseline again. Each leg captures the same four presets, two chord sizes and two voice policies. Its report requires passes 1, 2 and 3 for every combination. Compare both baseline legs before attributing a timing change to the candidate.

### Interleaved A/B Methodology
* Order of execution: `A B B A A B B A ...` (ABBA pairs cancel linear temperature or frequency drift).
* Every pass is launched as a **fresh process** to re-randomize heap and ASLR allocations.
* $K = 10$ pairs ($20$ passes total) yields ratios $\rho_k = M_{B,k} / M_{A,k}$.
* Non-parametric order statistics interval: $[ \rho_{(r)}, \rho_{(K+1-r)} ]$.
* For $K = 10$, $r = 2$ provides exact coverage:
  $$1 - 2 \cdot \frac{11}{1024} = 0.9785 \implies \mathbf{97.9\ \%}$$
* Verdict per load with $g = \text{min-gain} = 0.01$ (or measured E2 floor):
  - `GAIN`: $\rho_{(K+1-r)} < 1 - g$
  - `REGRESSION`: $\rho_{(r)} > 1 + g$
  - `NO-CHANGE-PROVEN`: Otherwise.
  - A claimed gain is accepted only when both millisecond timings and PMU cycles demonstrate `GAIN`.

### Automated Regression Gate (`scripts/perf-gate.py`)
Compares a 3-pass test run against the 5-pass baseline stored in `docs/perf/baseline-<device>.json`.

| Metric | Failure Threshold |
|---|---|
| p99 (points) | $\text{new\_med} > \text{base\_med} \cdot (1 + \max(0.03, 2 \cdot \text{spread\_base})) + 0.5$ |
| median block (points) | $\text{new\_med} > \text{base\_med} \cdot (1 + \max(0.03, 2 \cdot \text{spread\_base})) + 0.5$ |
| max block (points) | $\text{new\_med} > \text{base\_med} \cdot (1 + \max(0.10, 2 \cdot \text{spread\_base})) + 2.0$ |
| blocks over 100% | $\text{new\_med} > \text{base\_med} \cdot 1.2 + 2$ |
| `rss_kb` | $\text{new} > \text{base} \cdot 1.05$ |
| `majflt` | $> 0$ in timed region |
| `minflt_measured` | $> \text{base\_max}$ (no new page faults after warm-up) |
| Any row with `valid = 0` | Fails the gate immediately with **`GATE-INVALID`** (exit code 3); never averaged in |

---

## 6. CSV Schema v2 & Field Definitions

The output CSV file (`--csv`) records 41 columns across four logical groups:

### 1. Base Configuration & Timings
* `device`: Host device identifier (`k61`, `xl`, or desktop hostname).
* `cpu`: Pinned CPU core index.
* `cpu_info`: CPU part number, frequency governor, and initial frequency.
* `profile`: Quality profile string (e.g. `low`, `high,voices=16`).
* `settings`: Setting name (`settings`, `ladder`, `tiers`, `quick`).
* `load`: Name of benchmark load or preset.
* `group`: Preset category or `synthetic`.
* `notes`: Held polyphony note count.
* `voices_mean`, `voices_max`: Active voices during measurement.
* `sr`: Sample rate in Hz (typically 44100).
* `block`: Frames per audio block (typically 128).
* `blocks`: Total measured blocks in the run.
* `min_pct`, `median_pct`, `p99_pct`, `max_pct`, `mean_pct`: Execution time as percentage of real-time block budget ($100\% = 128 / 44100 \approx 2.9025\text{ ms}$).
* `min_ms`, `median_ms`, `p99_ms`, `max_ms`, `mean_ms`: Block execution time in milliseconds.
* `blocks_over_100pct`: Count of blocks exceeding $100\%$ real-time budget.
* `cpu_khz_after_run`: CPU clock frequency measured immediately after run completion.
* `pass`: 1-based pass index.

### 2. Run Health & Environment (Task T13)
* `p999_pct`: 99.9th percentile execution time percentage.
* `rt_prio`: SCHED_FIFO real-time priority applied to measuring thread.
* `thread_cpu_ms`: Thread CPU time (`CLOCK_THREAD_CPUTIME_ID`).
* `run_delay_pct`: Run queue wait share ($\text{wait\_ns} / (\text{run\_ns} + \text{wait\_ns})$ from `/proc/thread-self/schedstat`).
* `nivcsw`: Involuntary context switches during timed audio rendering.
* `minflt_run`, `minflt_measured`: Minor page faults during total run vs timed blocks.
* `majflt`: Major page faults during timed blocks (must be 0).
* `rss_kb`: Resident set size in KiB from `/proc/self/status`.
* `temp_c_max`: Highest temperature reading from system thermal zones in °C.
* `khz_before`: CPU clock frequency measured immediately prior to run start.
* `valid`: Binary health flag ($1 = \text{valid}$, $0 = \text{invalid}$).
* `invalid_reason`: Comma-separated contamination causes when `valid == 0`:
  - `stall`: A block exceeded 12 budget blocks ($> 34.8\text{ ms}$), indicating kernel RT throttling.
  - `run-delay`: Run queue wait share exceeded $0.002$ ($0.2\%$).
  - `freq`: CPU frequency dropped by $> 2\%$ during run ($\text{khz\_after} / \text{khz\_before} < 0.98$).
  - `majflt`: Major page faults occurred during audio processing.
  - `temp`: Maximum temperature exceeded $80.0^\circ\text{C}$.

### 3. Hardware PMU Counters (Task T15)
* `pmu_set`: Counter set (`compute` or `cache`).
* `cycles_mean`, `cycles_p99`: CPU cycles per block.
* `instr_mean`: Instructions retired per block.
* `ipc`: Instructions per cycle ($\text{instr\_mean} / \text{cycles\_mean}$).
* `l1d_refill_per_block`: Level 1 data cache refills.
* `l1d_access_per_block`: Level 1 data cache accesses.
* `l2d_refill_per_block`: Level 2 data cache refills.
* `l2d_access_per_block`: Level 2 data cache accesses.
* `br_mis_per_block`: Branch mispredictions per block.
* `pmu_scaling`: Counter active time ratio ($\text{time\_running} / \text{time\_enabled}$). $1.000$ indicates no multiplexing.

### 4. Cache Coldness & Initialization (Task T16)
* `cold_mode`: `warm` (default steady state after 344 warm-up blocks) or `evict` (cache eviction buffer flushed prior to every block).
* `first_block_ms`: Execution time of block 0 on a fresh engine (without warm-up).

---

## 7. Command-Line Reference: `synthyboy_tier_bench`

Complete list of options supported by `bench/tier_bench.cpp`:

| Option | Argument | Description |
|---|---|---|
| `--presets` | `DIR` | Directory containing `.sbpreset` factory preset files. |
| `--core` | `N` | Pin measuring thread to CPU core index $N$ via `sched_setaffinity` (Linux). Moves helper threads away to remaining cores. |
| `--set` | `NAME` | Preset profile set to run: `settings` (default low profile with individual options swept), `ladder`, `tiers`, or `quick`. |
| `--config` | `PROFILE` | Explicit quality profile string (e.g. `low`, `high,voices=16,control=economy`). Can be repeated. |
| `--load` | `NAME` | Benchmark load to execute: `worst`, `heavy`, `basic`, synthetic `dist:<type>`, `warp:<type>:<amount>`, or preset name/path. |
| `--all-presets` | - | Test every `.sbpreset` found in `--presets DIR` instead of default load list. |
| `--seconds` | `S` | Audio duration to render and measure per run in seconds (default 30.0 s; 2.0 s default with `--modules`). |
| `--notes` | `K` | Hold $K$ polyphony notes instead of profile/preset default voice limit. |
| `--retrigger` | `SEC` | Interval in seconds between note release and retrigger (default 1.0 s). |
| `--paced` | - | Pace audio block execution to wall-clock real time instead of rendering back-to-back. |
| `--csv` | `FILE` | Write per-run performance and health metrics to CSV file. |
| `--block` | `N` | Audio block size in frames (default 128). |
| `--sr` | `HZ` | Audio sample rate in Hz (default 44100.0). |
| `--no-ftz` | - | Disable flush-to-zero / denormals-are-zero floating-point hardware modes. |
| `--start-budget-us` | `N` | Spectral start budget in microseconds of start work per block (`Quality::spectralStartBudgetUs`), tier low ledger. Measurement only: profiles do not set it. Default 1700 (`spectral::kStartBudgetUs`; was 2400 until 2026-11-21, when the interleaved budget sweep on the Key 61 put 10 of 12 Spectral strike rows under 1.5 blocks at 1700 against 0 of 12 at 2400). |
| `--fx-budget` | `PCT` | `Quality::fxBudgetPct` of every profile: the FX cost budget in percent of a 128 frame block on the Key 61 (0 = unlimited, `docs/perf/fx-budget.md`). Default by the profile: 35 at tier low, none with `fx=all`, medium and high. |
| `--fx-budget-status` | - | After every run print `fx budget:` with the budget, the admitted cost and the cost requested without the budget (percent of a block) and the over-budget masks (`over modules` bit m = module m of instance 0, `over slots` bit s = slot s), as `IEngine::fxBudgetStatus` reports them. |
| `--start-cache` | - | `Quality::spectralStartCache` = 1: a note struck again takes its first Spectral frames from the process-wide start cache (bit-identical to the cache off, `docs/plans/spectral-start-cache.md`). Default: by the profile, on at tier low only. |
| `--no-start-cache` | - | `Quality::spectralStartCache` = 0: the start cache off (the A side of an A/B/A). |
| `--sections` | - | Report exclusive execution time spent in each DSP section (requires build with `-DSYNTHYBOY_SECTION_TIMING=ON`). |
| `--list` | - | Print the resolved list of quality profiles and loads that would run, then exit. |
| `--repeat` | `N` | Repeat entire benchmark sequence $N$ times (default 1). Summary reports median and spread. |
| `--verify` | - | Offline correctness verification: render each load twice (with and without optimizations) and check bit-exact / tolerance equivalence. |
| `--micro` | - | Run microbenchmarks on individual content DSP kernels (Sample, Granular, Spectral) and report microsecond timings. |
| `--tolerance` | `X` | Relative tolerance threshold for `--verify` mode (e.g. `3e-5`). |
| `--profile` | `FILE` | Collect statistical program-counter sampling profile of measuring thread on Linux ARM (250 us interval). |
| `--no-fx-mute` | - | Disable FX bypass optimization; process effects even when mix parameter is 0.0. |
| `--scalar-filters` | - | Force scalar filter kernels instead of SIMD/vector implementations. |
| `--scalar-warp` | - | Force scalar oscillator warp processing instead of 4-lane vector forms. |
| `--scalar-content` | - | Force per-frame reference implementations for content oscillators instead of block kernels. |
| `--no-voice-savings` | - | Disable voice allocation caches and modulation matrix evaluation skips. |
| `--budget` | `PCT` | Maximum acceptable 99th percentile block time percentage to declare a profile viable. |
| `--rt` | `N` | Run measuring thread under `SCHED_FIFO` with real-time priority $N$ (1..99). Requires root/CAP_SYS_NICE. |
| `--mlock` | - | Lock benchmark process virtual memory using `mlockall` to eliminate paging stalls. |
| `--rt-gap` | `MS` | Sleep `MS` milliseconds outside timed loop every 64 blocks to prevent kernel RT throttling (default 12.0 ms; 0 = no gap). |
| `--rt-require` | - | Exit with error code 1 if real-time scheduling priority cannot be acquired. |
| `--no-helpers-away` | - | Retain process default CPU affinity for DSP helper worker threads instead of migrating them away from the pinned measuring core. |
| `--retry-invalid` | `N` | Automatically repeat an invalid run up to $N$ times (default 2); last run is recorded. |
| `--pmu` | `SET` | Read hardware performance monitoring counters per block: `compute` (cycles, instructions, branch misses) or `cache` (cycles, L1D, L2D). |
| `--pmu-require` | - | Exit with error code 1 if hardware performance counters cannot be initialized. |
| `--evict` | - | Invalidate data caches before every block by reading/writing a buffer twice the largest cache size. |
| `--modules` | - | Measure standalone differential cost of every DSP module (effects, filter models, warp types, unison) against `basic` load. |
| `--no-aa` | `MASK` | Disable anti-aliasing algorithms by bitmask: bit 0 distortion ADAA, bit 1 warp ADAA, bit 2 BLEP, bit 3 BLAMP, bit 4 band limit, bit 5 library warp ADAA (63 = all off). |
| `--module-group` | `GROUP` | Filter module benchmark to specific subsystem: `fx`, `filter`, `warp`, `morph`, `unison`, `osc`, or `all` (default). |
| `--module-reps` | `N` | Number of repetitions per module test case to compute median differential cost (default 3). |
| `--first-blocks` | `N` | Measure prepare time and initial $N$ blocks (1..64) of freshly instantiated engines to quantify cold-start cost. |
