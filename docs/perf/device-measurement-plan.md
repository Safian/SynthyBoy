# Device measurement plan: code changes and compiler flags (2026-11-26)

Everything below runs on the Mac (offline builds, `ssh` to the devices on the local network). The cloud session cannot reach the devices. Nothing in this plan has been measured yet: every number in the commits it refers to is an x86 VM number.

Rules that apply to every step (CLAUDE.md, `docs/dsp-knowledge/measurement.md`):

- Interleaved A/B/A, device quiet (nobody playing, no install running). A difference counts only when every A run is on one side of every B run and the gap is several times the spread. A single run proves nothing.
- Key 61 bench: core 3, profile `low`, governor performance. XL: core 6, profile `high,voices=16,control=economy`. `scripts/perf-device.sh` sets both.
- Record the start and end temperature of every thermal zone. Stop and cool down when a zone comes within 10 C of its first passive trip (XL 75 C, Key 61 85 C).
- Never run the host-stop phase of `e1` on the XL while a project is open. Leave the XL on the package the current task needs and keep the accepted build installed afterwards.
- Raw evidence (CSV, per-pass stdout, package identity, sampling limits) goes under `.hw-local/mpc-perf/<label>/`. Pass it as `--out` to `perf-device.sh`.

## 0. What is on the branch

Branch `claude/stoic-mayer-rsr7pc` on top of `a1282cd`:

| Change | Commit | Expected effect | Audio |
|---|---|---|---|
| tier_bench rejects invalid measurements, `--self-test` | `56beb65` | none (tool only) | none |
| Block snapshot follows only changed parameters | `3a99afb` | fewer spikes while a knob moves | bit-identical (host tests) |
| Shared 4x warp decimation for >= 3 steady unison voices | `76b9d6e` | cheaper warp + unison presets at tiers medium/high/XL | NOT bit-identical, <= -120 dB of the block peak |

Source states for the A/B runs (create each as a separate `git worktree` and run `scripts/build-tier-bench.sh` from inside it, because the script builds the checkout it lives in):

```bash
git worktree add ../sb-A  a1282cd                      # A: before all changes
git worktree add ../sb-P  3a99afb                      # snapshot only (on top of a1282cd)
git worktree add ../sb-W  76b9d6e                      # shared decimator only (on top of a1282cd)
git worktree add ../sb-H  claude/stoic-mayer-rsr7pc    # all changes (the candidate)
```

`3a99afb` and `76b9d6e` were both committed on `a1282cd` in separate worktrees, so `sb-P` and `sb-W` each hold exactly one change.

## 1. Baseline first (TODO "measurement base")

Builds (Docker, Debian 12 cross compiler). Use `sb-H` as the baseline source so the section and module tables describe the code that ships:

```bash
cd ../sb-H
scripts/build-tier-bench.sh --arch arm32 --dynamic --sections --out ~/sb-out/H        # Key 61
scripts/build-tier-bench.sh --arch arm64 --dynamic --sections --out ~/sb-out/H-xl     # XL, only when the XL is free
```

Key 61 run (the XL is the same with `--device xl` and `PERF_BUILD_OUT=~/sb-out/H-xl`):

```bash
export PERF_BUILD_OUT=~/sb-out/H
L=.hw-local/mpc-perf/baseline-k61-$(date -u +%Y%m%d)
scripts/perf-device.sh --device k61 --phase probe   --out $L/probe
scripts/perf-device.sh --device k61 --phase install --out $L/install
scripts/perf-device.sh --device k61 --phase e0      --out $L/e0
scripts/perf-device.sh --device k61 --phase e2      --out $L/e2        # noise floor: gives the min-gain for every later verdict
scripts/perf-device.sh --device k61 --phase modules --out $L/modules
```

Sections (the `--sections` binary; its block times include timer overhead, use it only for the split):

```bash
python3 -I scripts/perf-tables.py sections $L/<sections stdout> > docs/perf/sections-k61.md
python3 -I scripts/perf-tables.py modules  $L/modules/<modules stdout> > docs/perf/modules-k61.md
python3 -I scripts/perf-gate.py baseline --out docs/perf/baseline-k61.json $L/e0/*.csv   # at least 3 passes
```

Check the exact stdout file names in `$L` first. The E2 result decides the `--min-gain` used below. If E2 does not meet its band, use the measured half-width.

Acceptance: `docs/perf/baseline-k61.json`, `sections-k61.md`, `modules-k61.md` exist (they are referenced by `docs/perf/README.md` but were missing from the repo); E2 is within `[0.99, 1.01]` for at least 11 of 12 loads, or the measured floor is recorded.

## 2. A/B/A of the two engine changes

`perf-device.sh --phase ab` runs the ABBA loop of the binary in the device slot `static` (A) against the slot `--bin` (B, default `dyn`) and calls `perf-ab.py`. Baseline in `static`, candidate in `dyn` (the slot name is only a directory; both binaries are the static build):

```bash
scripts/build-tier-bench.sh --arch arm32 --dynamic --sections --out ~/sb-out/A    # run inside ../sb-A
scripts/build-tier-bench.sh --arch arm32 --dynamic --sections --out ~/sb-out/H    # run inside ../sb-H (done above)

export PERF_BUILD_OUT=~/sb-out/A
scripts/perf-device.sh --device k61 --phase install --out $L/install-A      # A into /tmp/sbperf/static
# put the candidate's STATIC binary into the dyn slot, overwriting the dyn build installed above
COPYFILE_DISABLE=1 tar -C ~/sb-out/H/tier-bench-arm32 -cf - . | ssh root@192.168.0.30 "tar -C /tmp/sbperf/dyn -xf -"
scripts/perf-device.sh --device k61 --phase ab --bin dyn --out .hw-local/mpc-perf/ab-H-vs-A-k61-$(date -u +%Y%m%d)
```

Read `ab-ab.txt`: per (profile, load) verdict `GAIN`, `REGRESSION` or `NO-CHANGE-PROVEN`. Repeat with `sb-P` and `sb-W` in the candidate slot to attribute a gain to one commit.

What this can and cannot show:

- The WT decimator change is visible in loads with warp and >= 3 unison voices (`Hyper Saw Lead`, `Bode Barber Riser`, `worst`, `heavy` are candidates; the factory presets contain none with warp and >= 3 steady unison voices on oscillator 1 per the worker's check, so the effect on the factory set may be zero). `perf-device.sh --loads` accepts only `set12` or `set6:<preset names>`, not `warp:` loads. To cover the changed path add the preset names to a `set6:` list, or run `synthyboy_tier_bench` on the device by hand with `--load "warp:<type>:<amount>"` (3 unison voices, `docs/perf/README.md` section 4) in the same ABBA order and feed the CSVs to `scripts/perf-ab.py`. The change is gated to tiers medium, high and XL: the Key 61 `low` profile (2x, 32 taps) keeps the old path by design, so a Key 61 `low` run is expected to show `NO-CHANGE-PROVEN` for it. Test the Key 61 at a `medium` profile as well if the owner wants that tier on the Key 61. Do not claim a Key 61 gain from `low`.
- The block-snapshot change only helps in blocks where a parameter changed. `tier_bench` has no parameter-automation load, so the steady-state ABBA run will show no difference by design. Its effect (knob-move spikes) needs a dedicated load or a Q-Link sweep on the device with worst-block statistics, 3-5 runs per variant (`measurement.md`: worst-block noise is about +-1 ms). Not covered by an existing script; decide with the owner whether to add a tier_bench load that changes one parameter every N blocks.
- The decimator is not bit-identical. Before accepting it on a device, listen to a warp + unison preset on the Key 61/XL once, and record the verdict next to the measurement.

## 3. Compiler flags A/B/A (TODO: Cortex-A17/A76 tuning, LTO, `-fno-math-errno`)

All variants build from the SAME source (`sb-H`), differing only in flags, using the new `--cxx-flags` option of `scripts/build-tier-bench.sh` (flags are appended to `CMAKE_CXX_FLAGS_RELEASE`; the toolchain's `-march` stays). One output directory per variant:

```bash
cd ../sb-H
B=scripts/build-tier-bench.sh
$B --arch arm32 --out ~/sb-out/F0                                        --build-dir ~/sb-build/F0
$B --arch arm32 --out ~/sb-out/F1 --cxx-flags "-mtune=cortex-a17"        --build-dir ~/sb-build/F1
$B --arch arm32 --out ~/sb-out/F2 --cxx-flags "-fno-math-errno"          --build-dir ~/sb-build/F2
$B --arch arm32 --out ~/sb-out/F3 --cxx-flags "-flto"                    --build-dir ~/sb-build/F3
$B --arch arm32 --out ~/sb-out/F4 --cxx-flags "-mtune=cortex-a17 -fno-math-errno -flto" --build-dir ~/sb-build/F4
```

XL: same with `--arch arm64` and `-mtune=cortex-a76` instead of `-mtune=cortex-a17` (core 6 of the RK3588 is an A76).

| Variant | Flags | Expected | Risk |
|---|---|---|---|
| F1 | `-mtune=cortex-a17` / `-mtune=cortex-a76` | scheduling only, same ISA | audio should be bit-identical; verify |
| F2 | `-fno-math-errno` | inline `sqrt`, no errno writes | bit-identical for finite input; NaN/negative input paths must be checked |
| F3 | `-flto` | cross-TU inlining | needs the LTO plugin in the cross compiler (check the build log); static link; bit-identical expected |
| F4 | F1+F2+F3 | only after F1-F3 each showed GAIN | |

Do NOT use `-mcpu=cortex-a17` or `-mfpu=neon-vfpv4` as "just tuning": VFPv4 enables fused multiply-add (GCC contracts by default), which changes the float results, so it is not a bit-identical experiment. If the owner wants it, run it as a separate variant under the tolerance rules of `tier_bench --verify` and confirm the VFPv4 flag in `/proc/cpuinfo` features on the device first. That is a hypothesis, not a measured fact.

Interleaved runs (one variant against F0, same procedure as section 2, `static` slot = F0, `dyn` slot = the variant):

```bash
for V in F1 F2 F3; do
  export PERF_BUILD_OUT=~/sb-out/F0                                   # needs tier-bench-arm32{,-dyn,-sections}; reuse the section-1 install of H or rebuild F0 with --dynamic --sections
  scripts/perf-device.sh --device k61 --phase install --out .hw-local/mpc-perf/flags-$V/install
  COPYFILE_DISABLE=1 tar -C ~/sb-out/$V/tier-bench-arm32 -cf - . | ssh root@192.168.0.30 "tar -C /tmp/sbperf/dyn -xf -"
  scripts/perf-device.sh --device k61 --phase ab --bin dyn --out .hw-local/mpc-perf/flags-$V-$(date -u +%Y%m%d)
done
```

The `install` phase needs all three variant directories (`tier-bench-arm32`, `-dyn`, `-sections`) in `PERF_BUILD_OUT`. Build F0 once with `--dynamic --sections` for that purpose, or reuse the `H` output of section 1 as F0.

Decision rule (from `docs/perf/README.md` section 5): a variant counts as a gain only when both `median_pct` and PMU cycles show `GAIN` with the E2 floor as `--min-gain`; run `--phase pmu` first to confirm `pmu_scaling == 1.000`. Without PMU, label the result "timing only".

### Audio identity check for flag variants: gap

`tier_bench --verify` compares savings against references inside one binary. It does not compare two builds. The existing bit-identity tool is `synthyboy_preset_render --float32 --hash` plus `scripts/compare-renders.sh`, but `scripts/build-tier-bench.sh` builds only `synthyboy_tier_bench` and the two probes. A Mac render (arm64) does not prove armv7 identity. To close this gap one of these is needed (not done yet, ask for it):

1. cross-build `synthyboy_preset_render` for arm32/arm64 with the same `--cxx-flags` and run it on the device (needs the build script to accept an extra target), or
2. a `--hash` output mode in `tier_bench` that prints the float-bit hash of a rendered load, compared across the F0 and variant binaries.

Until then F1-F3 results are "faster, audio identity unverified". Do not install a flag variant as the release build on that basis.

## 4. In-host measurement with the native overlay package

`synthyboy_tier_bench` renders the engine alone. The release plugin runs with the SynthyBoy native editor and the display layer (overlay) enabled (CLAUDE.md, "Release packaging"). The overlay thread and its helper compete for the same cores as the host's audio threads: measured on the Key 61 the overlay costs about 8 points of one core idle and about 15 under a chord, the `sb-overlay` thread 5.4-9.7 % of a core and the helper 5-7 % (`measurement.md`, "Overlay thread split"). A gain in the engine-only bench is therefore necessary but not sufficient: confirm it in the installed package with the native skin.

Rules for the in-host runs:

- Build ONLY the architecture of the device being measured (Key 61: `arm32`, XL: `arm64`). Both are built together only for a release or release candidate.
- Package A and package B are built from the same source state except for the change under test, with the same committed skin (`skin/`, the `build-mpc.sh` default), the same `MPC_SDK`, and the same options. Do not pass `--skins`, `--no-validate` or a different skin: a missing or broken native skin must fail the build and the install before the working package is replaced.
- Build each package from its own worktree (section 0) with its own `--out` and `--build-dir`. Add `--bench` so the note-on bench of the same code is built in the same run.
- Install with the checked installer (`scripts/k61-install.sh PACKAGE_DIR` / `scripts/xl-install.sh PACKAGE_DIR`, see `.agents/skills/akai-mpc-ssh-deploy`). Record the package identity (sha256 of `synthyboy-<arch>.so`, of the overlay helper, and of `Plugin Skins/TUI.json`) with the evidence.
- Keep the accepted package installed between tasks. On the XL leave the package the current task needs. Reinstall the accepted package after the run.

```bash
export MPC_SDK=/path/to/mpc-sdk                       # required by build-mpc.sh
cd ../sb-A && scripts/build-mpc.sh --arch arm32 --bench --out ~/sb-pkg/A --build-dir ~/sb-build/mpc-A
cd ../sb-H && scripts/build-mpc.sh --arch arm32 --bench --out ~/sb-pkg/H --build-dir ~/sb-build/mpc-H
# on the Key 61, order A, H, A: install, let the host start, run the load, collect, then the next package
scripts/k61-install.sh ~/sb-pkg/A      # then the measurement below, then:
scripts/k61-install.sh ~/sb-pkg/H      # ... then A again for the second A leg
```

What to measure in the host (per leg; record start and end temperature of every thermal zone):

1. **Overlay active and inactive.** Run each load once with the overlay on (the page of the native editor open, a visible page that animates, e.g. Macros or an LFO page) and once with it off or the plugin page hidden (host draws 0 frames when idle, `mpc-display.md`). Per-thread CPU from `/proc/<pid>/task/*/stat` for the MPC main thread, the audio worker threads, `sb-overlay` and the helper, sampled over a fixed window (the method of `.hw-local/mpc-perf/key61-voice-master-resources-20261125/`). Process-level samples give shares and cannot separate allocations inside host widgets: state that limit with the numbers.
2. **Engine under that load.** `run-noteon-burst.sh LABEL BASELINE_BINARY CANDIDATE_BINARY` (private, in `.hw-local/mpc-perf/noteon-burst/`) with the `synthyboy_noteon_bench` from `~/sb-pkg/A/bench-arm32/` and `~/sb-pkg/H/bench-arm32/`: baseline, candidate, baseline again, same four presets, two chord sizes, two voice policies. It reports every combination in passes 1, 2 and 3; compare the two baseline legs before attributing a change to the candidate.
3. **Knob-move spikes (block snapshot change).** With the native editor open and the overlay on, move one Q-Link continuously for a fixed time on a voice parameter and on an effect parameter, and record the worst-block statistics (3-5 runs per package, worst-block noise is about +-1 ms). This is the only place the snapshot change shows its benefit; `tier_bench` cannot.
4. **Display path.** The two engine changes do not touch the display path, so no display acceptance is needed for them. For any later change that does touch it, additionally check the active and the inactive state on the requested MPC, and what the host shows while the overlay is off and after it returns (CLAUDE.md). Screen captures in `docs/screens/` are refreshed from the deployed hardware by the owner after a Mac build (`mpc-screen-photo` skill); nothing in this plan writes there.

Save everything under `.hw-local/mpc-perf/<label>/`: raw samples, package identity, the exact commands, and the sampling limits. If an earlier derived result is disproved by a later one, delete it and link the replacement.

## 5. Order on the day

1. Baseline (section 1) on the Key 61 and E2 noise floor.
2. A/B/A of the engine changes (section 2).
3. Flag variants F1, F2, F3 (section 3); F4 only if all three gain.
4. In-host confirmation with the native overlay package (section 4) for everything that showed a gain; a result that exists only in the engine bench is not accepted.
5. XL repeats of sections 1-4 when the XL is free; keep the accepted package installed afterwards.
6. Commit the three docs tables, update `docs/dsp-knowledge/measurement.md` with the method and spread, delete finished items in `TODO.md`.
