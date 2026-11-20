# Plan: spreading the note start of the Spectral oscillator over several blocks (stagger Spectral priming)

Status: plan only, written from code and document reading (no build, no test, no benchmark, no device was run). Branch `worktree-agent-ab863092791c8892f`, base `f1fddd71` (develop). No file under `core/` is changed by this document.
Covers: the owner's decision of 2026-11-20 ("stagger Spectral priming", `TODO.md:101`, release item, first of the DSP audit part 3 section) and the audit work-list item "cap burst note-ons and stagger Spectral priming" (`docs/review/OWN-ENGINE-DSP-AUDIT-3-CORE-PERFORMANCE-2026-11-20.md:37,145,347,497`, verified in `...-3-VERIFICATION-2026-11-20.md:17`).
Open point for the owner: the budget constant of section 3.4 is provisional until the device measurement M1 (section 2) has run; the task list (section 4) contains the calibration step and the rule for it.

Conventions: `A:l` = file:line that was opened for this plan. "tick" = one control period = `kControlPeriod` = 32 samples (`core/engine-own/dsp/util.h:21`) = 32 / 44100 = 0.7256 ms at 44.1 kHz. "quad" = 4 ticks = 128 samples = 2.9025 ms = one host block of the Key 61 (`docs/MPC.md:321`: 2.902 ms). "warm us" = microseconds of the warm kernel on the Key 61 (Cortex-A17, 1608 MHz) as printed by `--micro` and quoted in `docs/engine-spec/07-sample-granular-spectral.md:74`. "voice" below = one `SpectralVoice` (one unison voice of one oscillator of one engine voice), not an engine voice.

---

## 1. The problem, as verified

### 1.1 What the code does today

| # | What | Where (opened) |
|---|---|---|
| a | The Spectral oscillator is one `LibOsc` per slot and engine voice; its unison voices are `specVoices` (`kSpecU = 4`, so at most 4 per oscillator). `LibOsc::update` runs once per control period; at the first call of a note (`noteStarted[type]` false) it seeds the random generator and calls `beginType`. | `core/engine-own/osc/lib_osc.h:94,201`; `lib_osc.cpp:366,668-672` |
| b | `beginType`, Spectral branch: for every unison voice `u` it calls `SpectralVoice::noteOn` at once, with the start offset `quarter * kStartStep`, `quarter = (serial * U + u + 2 * oscIndex) mod 4`, `kStartStep = 128`. The note serial is the engine's note counter. | `lib_osc.cpp:762-773`; `core/spectral/include/synthyboy/spectral/spectral.h:29-30`; `own_engine.cpp:815` |
| c | The engine calls the new voice's first `tick` (and so `beginType`) inside the note-on handler, not at the next control period. `controlTick` (the tick of all active voices) runs at the start of a control period, before that period's events. | `own_engine.cpp:876` (`chosen->tick`), `:1169`, `:1222` (`forEachActive ... tick`), `:1492` (`controlTick()` at `controlPos == 0`), `:1608-1609` (events handled after it, tier low) |
| d | `SpectralVoice::noteOn` makes, per channel, the frames of the priming that the schedule says are due at offset `d` (`startSchedule`, `startFrames`) and nothing else of the later work: the strike mask is built at `spectral.cpp:632-641`, the frames are made at `:657`, the play head takes its first step at `:665`. | `core/spectral/src/spectral.cpp:571-668` |
| e | The schedule (computed from the squared Hann window, limit `kMaxMissingWeight = 0.5`): frame `j` is made before its squared window at the sample would pass 0.5, at a multiple of 128, and at the latest at sample 512 (the first regular frame). | `spectral.cpp:1096-1100` (limit), `:1134-1144` (`startSchedule`), `:1227-1296` (`startFrames`) |
| f | Deferred frames are made inside `process` when the voice's output position `outPos_` reaches the join sample, and at the end of the first hop (`endFirstHop`); `process` cuts its runs at these samples, so the work lands in the control period that contains the sample. | `spectral.cpp:1435-1491` (`process`), `:1313-1331` (`endFirstHop`) |
| g | Kill mode: the voice beyond the polyphony limit is given up at the note-on (`allocateVoice`, `killOverflowVoice`). This is commit `211020a7` (the task text names `5ef901c8`, which is a later `TODO.md` commit: `git log -1 5ef901c8`). The voice that is given up was already started if it was struck earlier in the same burst (its `chosen->tick` at `own_engine.cpp:876` ran `beginType`). | `own_engine.cpp:674-695`, `:876` |
| h | Tier low: `eventGrid` is true when `kEconomyControl` is set; events then take effect at the start of the control period that contains them, so the engine's ticks and the events are on one 32-sample grid whatever the host block size. | `own_engine.cpp:1480,1608-1609`; `core/include/synthyboy/quality.h:68,100-104` |

### 1.2 What it costs (numbers, with the arithmetic)

Cost of the strike per voice, from the kernel times of `docs/engine-spec/07-sample-granular-spectral.md:74` (Key 61, `--micro`): one frame = phase advance 33 + sine/cosine 17.5 + inverse transform 33.5 + overlap add 4.7 = 88.7, called **89 us**; one shaping = **67 us** (scalar `findPeaks`, 32-bit ARM). A stereo sample doubles both (the loops run per channel: `spectral.cpp:1235-1243`).

Work of the start by offset `d`, per channel (frames `f`, shapings `s`; the schedule of `spectral.cpp:1134-1144` evaluated: with `w(x) = 0.5 - 0.5 cos (2 pi x / 2048)`, frame `j` at sample `q` weighs `w(q + (3 - j) * 512)^2`; `w^2 > 0.5` holds for `x` between 651.4 and 1396.6, so frame 3 (`x = q < 512`) never passes inside the first hop and is made at 512; frame 2 (`x = 512 + q`) passes from `q = 140`, join = `max (d, 128)`; frame 1 (`x = 1024 + q`) passes from `q = d` while `1024 + q < 1396.6`, i.e. for `d <= 372`, join = `d`, and for `d = 384` it never passes, join = 512; this agrees with `docs/engine-spec/07:68`):

| offset d | at the strike (tick 0) | later | total |
|---|---|---|---|
| 0 | frame 1: 1 f, 1 s = 156 us | frame 2 at sample 128 (tick +4): 156 us; frame 3 at 512 (tick +16): 156 us | 3 f, 3 s = 468 us |
| 128 | frames 1, 2: 2 f, 1 s = 245 us | frame 3 at the hop end, 512 - 128 = 384 samples (tick +12): 156 us | 3 f, 2 s = 401 us |
| 256 | frames 1, 2: 245 us | frame 3 after 256 samples (tick +8): 156 us | 401 us |
| 384 | frame 2: 1 f, 1 s = 156 us | frames 1 and 3 at the hop end after 128 samples (tick +4): 2 f, 1 s = 245 us | 401 us |
| any, Smear warp | frames 1..3 and the phantom shaping of the frame 0 that is never made: 3 f, 4 s = 535 us | nothing | 535 us |

(156 = 89 + 67, 245 = 2 * 89 + 67, 468 = 3 * 156, 401 = 245 + 156, 535 = 3 * 89 + 4 * 67. `spectral.cpp:1239-1251` is the Smear case: one phantom shaping when `phantom && smears`, then a shaping per frame.) The regular frame of the hop end (`synthesizeHop`) is not part of the start; it is the steady state: one frame and one shaping per voice every 512 samples = 156 / 4 = 39 us per block per voice.

Strike of a chord, as the docs measure it: eight voices (four notes, unison 2) have offsets 0,1,2,3,0,1,2,3 (`lib_osc.cpp:771`): 12 frames and 8 shapings = 12 * 89 + 8 * 67 = 1068 + 536 = **1604 us warm** (`docs/MPC.md:354`: "1.6 ms"). The cold strike block measured on the device is far above that: `docs/MPC.md:354` "the benchmark's block takes 5.4 ms in the priming section ... cold caches ... about three times the warm time" (HYPOTHESIS for the factor, decided by M1 below).

Measured before (develop `414b46af`, `tier_bench --core 3 --config low`, a chord every second, worst block of 3 passes x 4 s, block 2.902 ms; `.hw-local/mpc-perf/noteon-burst/before-414b46af.md`):

| preset | 8 notes (kill / steal) | 16 notes (kill / steal) |
|---|---|---|
| Spectral Vowel Freeze | 2.50 / 2.06 blocks | 3.93 / 3.69 |
| Spectral Gong Pad | 1.98 / 2.01 | 3.69 / 3.51 |
| Spectral Chord Morph | 2.11 / 1.98 | 3.87 / 3.45 |
| Acid Saw Reso (not spectral) | 0.60 / 0.58 | 0.78 / 0.66 |

The presets: `master.polyphony` 4, 4 and 3, `osc1.type 3` (Spectral), `osc1.unison.voices 2` (read from `content/presets/Pad/spectral_vowel_freeze.sbpreset`, `.../spectral_gong_pad`, `FX/spectral_chord_morph`); Acid Saw Reso: polyphony 16, wavetable. So a chord of 4 notes starts 8 `SpectralVoice`s; at 2.0 blocks = 5.8 ms the strike block is 5.8 - 1.7 (the p99 block of the same rows, 0.56 .. 0.62 blocks = 1.64 .. 1.79 ms) = about 4.1 ms above the usual block for 1.6 ms of warm work: a factor of 2.6. The 16-note rows are the burst before the Kill fix (`211020a7`, which is on develop now): they measured more voices started than the limit.

### 1.3 How it sounds, and what is wrong with the audit text

* Nothing sounds wrong today when the block is made in time; the harm is the overrun (a block of 5.8 .. 11.4 ms against a deadline of 2.9 ms is 1 .. 3 dropped host blocks on the Key 61: an audible dropout at every chord strike of a Spectral preset).
* The audit (`AUDIT-3:130`) says the 8-note Spectral chord overruns "2.6-3.1 blocks (`docs/MPC.md:353-354`)". `docs/MPC.md:354` speaks of eight **voices** (a four note chord with unison 2) measured in round 10, not of eight notes; the measured rows of today are 2.0 .. 2.5 blocks for 8 notes at polyphony 3 .. 4 (table above). It also says a 16-note burst stalls "more than 15 ms": the measured worst is 11.41 ms (3.93 blocks). Both corrections lower the numbers, they do not remove the problem.
* The audit's remedy "max 2-4 voices primed per 128-frame block" (`AUDIT-3:37`) is of the right size (section 3.4 arrives at 3 voices a block by arithmetic) but counts voices, which hides that a voice's start work depends on its offset (156 .. 245 us at the strike) and on the later frames (the audit forgets them: "the next block after a strike carries 8 more frames", `docs/MPC.md:354`).
* The audit's other half, "cap burst note-ons to the polyphony before `start()`/`tick()`", is **done in part**: `211020a7` gives up the voice that the limit picks at the note-on. It does not prevent the early voices of a burst from starting (they start at their own note-on, `own_engine.cpp:876`, before the later notes make them victims), so a 16-note burst still pays the start of the voices that die. The plan below fixes that as a side effect: a voice that waits and is killed costs nothing (section 3.4).
* The audit's proof tool, `synthyboy_noteon_bench`, "cannot load presets or play 16 notes" (`TODO.md:48`); the measurement that exists is `tier_bench` through `run-noteon-burst.sh`. The plan uses that.

---

## 2. The measurement to make BEFORE any change

All on the Key 61 (`root@192.168.0.30`; the coordinator's scripts take the device lock), because the cost that matters is the cold-cache block, which no desktop run reproduces (`docs/MPC.md:354`). Nothing here needs a code change.

**M0, baseline of the base commit.** Build the arm32 `synthyboy_tier_bench` of `f1fddd71` with `scripts/build-tier-bench.sh --arch arm32`, then
`bash /Users/szabi/Documents/Projects/Safian/.hw-local/mpc-perf/noteon-burst/run-noteon-burst.sh before-f1fddd71 <path of the arm32 synthyboy_tier_bench>`
(`.hw-local/mpc-perf/noteon-burst/run-noteon-burst.sh:1-12`: 16 rows, core 3, config low, 4 s x 3 passes, chord every 1 s, 8 and 16 notes, Kill and Steal). Record: the 16 "worst blocks" of `before-f1fddd71.md` (develop now has the Kill fix `211020a7`, so the 16-note rows are expected lower than the `414b46af` table). Run it once; it is the "before" of the acceptance.

**M1, what a strike costs as a function of the number of voices (decides the budget).** Same binary, same device, one preset, notes 1, 2, 3, 4 (so 2, 4, 6, 8 `SpectralVoice`s with unison 2), 4 s x 3 passes, chord every second:
`SYNTHYBOY_CONTENT_DIR=<content> <bench> --presets <dir> --core 3 --config low --notes N --seconds 4 --repeat 3 --retrigger 1 --load 'Spectral Vowel Freeze'` for N = 1, 2, 3, 4 (the form of `run-noteon-burst.sh`, line `args`). Record per N: the worst block (ms) `W(N)` and the p99 block `P99(N)`. Fit `W(N) - P99(N) = F + w * V` with `V = 2 N` voices. What it decides:
* if `F` is below 0.3 ms: the cold penalty is per voice, `kappa = w / 0.200 ms` (0.200 ms = the warm strike of an average voice: 1604 us / 8 = 200 us, section 1.2); the budget formula of section 3.4 holds as written;
* if `F` is 1 ms or more: a fixed first-touch penalty (tables, analysis frame, rings) that no stagger can cut, and task S3b of section 4 (pre-touching the start tables in the block before the strike) becomes part of the item.
HYPOTHESIS to be decided: `kappa = 3` (the reading of `docs/MPC.md:354`), against `kappa` 1.3 .. 1.75 if the 8-note rows of the table above started 16 voices.

**M2, steady block without a strike (the base of the budget).** The p99 of the same rows in `before-f1fddd71.md` (`before-414b46af.md`: 1.64 .. 1.79 ms = 0.56 .. 0.62 blocks for the Spectral presets). Used as `base`.

**M3, determinism before the change (CHECKSUM baseline).** The test of S5 prints `SPECSTART-CHECKSUM` lines for flag off; the build worker runs the test once on the base commit and once after S5 and compares (section 4).

No existing desktop tool measures the strike cost; the cost model of S3 is checked against counted work by a unit test, not by time.
