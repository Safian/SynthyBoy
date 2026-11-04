# Request: CPU benchmark on the Akai MPC Key 61

For the hardware session (the only one that may touch the MPC). Nothing in this repository connects to the device.
Goal: real numbers for the quality tiers of docs/ARCHITECTURE.md §3.6, which are targets until measured here.
`docs/PERFORMANCE.md` currently has laptop numbers only.

## 1. The binary

`synthyboy_bench-armv7` is built by CI (job `core tests (linux-armv7, qemu)`, artifact `synthyboy-bench-armv7`): ARMv7 hard-float,
NEON, **statically linked** (no dependency on the MPC's glibc/libstdc++), plus `SHA256SUMS`. Take it from the latest green run
of `main` on https://github.com/Safian/SynthyBoy/actions:

```sh
gh run download <run-id> --repo Safian/SynthyBoy -n synthyboy-bench-armv7 -D bench-armv7
(cd bench-armv7 && shasum -a 256 -c SHA256SUMS)
```

Do not run anything that is not this file with the checksum above. The binary is single threaded, reads nothing, writes
only to stdout, opens no audio device and no network connection.

## 2. What to record on the device (before the runs)

```sh
uname -a; cat /proc/cpuinfo; nproc; cat /proc/loadavg
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq 2>/dev/null
```

Also note which core(s) the Kick audio thread uses (the two isolated cores) and whether the Kick app / `acvs` was running
during the run. The benchmark must run on one of the isolated cores (e.g. `taskset -c <isolated-core> ...`); say which.
Any stop/restart of the stock software is the hardware session's / owner's decision, not part of this request.

## 3. Commands

```sh
./synthyboy_bench-armv7 --engine own   --seconds 10 --block 128 --sr 48000
./synthyboy_bench-armv7 --engine vital --seconds 10 --block 128 --sr 48000
./synthyboy_bench-armv7 --engine own   --seconds 10 --block 64  --sr 48000
./synthyboy_bench-armv7 --engine vital --seconds 10 --block 64  --sr 48000
```

Each run prints the configuration and one result per quality tier (Low 8 voices, Medium 16, High 32) for saw and sine:
real-time factor, mean / p99 / worst block time in ms. Repeat each command 3 times (once right after boot, once warm) and
send all raw outputs unedited.

## 4. Expected result / how to read it

- The program finishes without error; no crash, no `Illegal instruction` (that would mean the NEON assumption is wrong for the
  real CPU: report the `cpuinfo` flags line).
- Real-time factor above 1 means the engine renders faster than real time on that core; the useful figure for tiers is the
  **worst block time versus the block budget** (block / sample-rate: 2.667 ms for 128 frames at 48 kHz, 1.333 ms for 64).
  A tier is only usable on the MPC if the worst block stays well under the budget with headroom for the rest of the Kick audio
  thread work.
- Nothing is promised in advance; on the development laptop the Vital engine costs about 9x the own engine per voice.

## 5. Where the results go

Paste the raw outputs and the device facts from §2 into a new section "Akai MPC Key 61" of `docs/PERFORMANCE.md` (or send them
to the session that owns this repository, which does it). The tier numbers in ARCHITECTURE.md §3.6 are then updated from them.
