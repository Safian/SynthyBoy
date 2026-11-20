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
| h | Tier low: `eventGrid` is true when `kEconomyControl` is set; events then take effect at the start of the control period that contains them, so the engine's ticks and the events are on one 32-sample grid whatever the host block size. | `own_engine.cpp:1480,1608-1609`; `core/include/synthyboy/quality.h:68,100-105` |

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

---

## 3. Design options and the chosen design

### 3.1 What the specification says (kept unchanged)

* `docs/engine-spec/07-sample-granular-spectral.md:59`: "The voices of a note and of a chord start at different offsets inside their hop (section 4a), so that their later frames do not fall into the same block." Kept: the offset formula `((s U + u + 2 o) mod 4) * 128` (`:72`, `lib_osc.cpp:771`) does not change; a voice that has to wait starts later **with the offset it would have had**.
* `:68`: the schedule "is in samples of the voice, made inside `process` at the sample, so it does not depend on the host's block size (blocks of 32 .. 512 give the same samples, 48 kHz and 44.1 kHz)". Kept: the start of a voice is delayed by whole control periods, and from its start on it is the voice of section 4a, sample for sample.
* `:57` "Random numbers: a per-oscillator deterministic generator (a render repeats)" and `docs/engine-spec/README.md:53` ("the engine renders in chunks of at most 128 frames ... cut at every note event"; tier low snaps events to the 32 sample grid, `own_engine.cpp:1608`). Kept: every decision below is a function of the control-period count since the engine's reset, the note serials and the events, nothing else (no clock, no thread state, no host block size).
* `docs/engine-spec/07:60-72`: everything about the frames, the renormalisation, the "identical from the first regular frame on" tests. Not touched.

New text for `07` section 4 (task S6): "At tier low a voice whose start does not fit the start budget of the current block starts at the first control period at which it fits; until then it outputs silence."

### 3.2 Options

**Option A: an engine wide start ledger, voice by voice (admission over a ring of future blocks).** The engine keeps, for the next five blocks (quads), the warm microseconds of start work that already admitted voices will do there. A `SpectralVoice` asks for its start; it gets it when its pattern (the table of section 1.2) fits under a budget `B` per quad in every quad it touches, in strict first come first served order (note serial, oscillator, unison voice); otherwise it waits one control period and asks again. A waiting voice outputs silence.
* Sound: nothing changes for a strike that fits (bit-identical to today, section 3.4 shows which strikes fit); a voice that waits starts later by whole control periods (0.73 ms steps), with the same offset, the same frames, the same samples from its start on. The envelopes and other oscillators of an engine voice whose Spectral oscillator waits run on (section 3.4, "what a waiting voice outputs").
* CPU: the strike block is bounded by the budget; the total work is unchanged (the same frames are made, later). The ledger costs a few integer operations per control period (a ring of 8 integers, a registry that is empty except while voices wait).
* Memory: about 5 KB (a registry of 128 entries of about 40 bytes: key, pattern 5 x uint16, two tick counters, a flag), inside the engine's existing `spectral::Scratch` (about 40 KB, `spectral.h:88-110`).
* Real-time safety: fixed arrays, integers, no allocation, no lock, no system call; a full registry grants (fail open, never blocks the sound).
* Risk: medium-low. Touches the `LibOsc` start path and two engine calls; every effect is behind one flag (tier low) and a flag-off render is bit-identical to today.

**Option B: the whole engine voice waits (the voice's tick and render are skipped until it is admitted).** The same ledger, but the unit is the engine voice, so envelopes, LFOs, the other oscillators and the Sub start together; the Spectral layer never lags its own envelope.
* Sound: best of all options for layered patches (no flam between the Spectral layer and a layered wavetable oscillator of the same voice, no envelope that has advanced).
* CPU / memory: as A.
* Real-time safety: as A.
* Risk: high. `Voice::tick` and `render` would be skipped for a voice that the engine already started, and every reader of per-voice state sees a voice that was started and never ticked: `ghostKill` reads `lastBusLeft/Right` of the victim (`own_engine.cpp:699-716`; stale values of the previous note on a reused voice would be faded in as a click), `updateLowestNote` / `tunedKeyNote` (`:747`), the voice count gain (`:1625`), `inheritEnvelopes`, legato and portamento state (`:866-880`), and `Voice::release` on an envelope that has not started (`voice.cpp:360-371`: a note-off during the wait would make the voice die silent, so a 10 ms tap of a pad vanishes). Each of these needs a rule and a test; the gain is only for layered patches whose delay is 3 .. 20 ms. Rejected for this item; the follow-up if listening shows the lead of the layers to be audible (section 6).

**Option C: make the strike cheaper instead of later (fewer frames at the strike, fewer voices).** From `docs/MPC.md:354`: (a) raise the limit of the missing window weight from 0.5 to 0.75 (moves 4 of the 12 frames of eight voices to the next block; noise gets more than the 0.5 .. 0.7 dB renormalisation error it has, `07:70`); (d) fewer Spectral voices at the strike (unison 1 at tier low).
* Sound: (a) changes the first hop of every Spectral note (noise up to 1.5 dB, more with 0.75); (d) changes the character of every Spectral preset with unison 2.
* CPU: (a) saves 4 * 89 = 356 us of 1604 us (22 %): strike block `1800 + 3 * (1604 - 356) = 5544 us` = 1.91 blocks at `kappa` 3 (section 3.4): not under 1.5. (d) halves the work: `1800 + 3 * 802 = 4206 us` = 1.45 blocks for eight voices, nothing for a chord of eight notes.
* Risk: sound change, the owner's decision (`docs/MPC.md:354`: "(a), (d) change the sound, which is the owner's decision"). Not chosen: A reaches the target without changing the sound of any strike that fits.

**Option D: a fixed delay by position (`serial mod K` quads), no budget.**
* Sound: a lone note on a later slot is delayed (2.9 ms per slot), which breaks "a single note is not delayed at all"; the delays do not follow the cost (a Smear voice costs 535 us, an offset 0 voice 156 us).
* CPU: the strike is spread but not bounded; no feedback from the real cost.
* Rejected: A is the same machinery with a cost model, and delays only what must be delayed.

### 3.3 The chosen design: option A, per voice, offsets unchanged

Reasons: (1) it is the only option that leaves the sound of every strike that fits bit-identical to today (testable by CHECKSUM); (2) it bounds the strike block by arithmetic (a budget per block), not by hope; (3) it is confined to the start path of the Spectral branch of `LibOsc`, a header-only ledger and two engine calls; (4) it removes the second half of the audit finding too: a voice that waits and is then given up by the polyphony limit has cost nothing (section 3.4); (5) it is a quality setting (`Quality::spectralStart`, on at tier low, S2), so the XL, iOS and desktop keep today's sound and today's instant start.

### 3.4 The chosen design in detail

**Units and constants** (all in a new header `core/spectral/include/synthyboy/spectral/start_ledger.h`):
* `kStartFrameUs = 89`, `kStartShapeUs = 67` (warm us on the Key 61, `07:74`); the number of channels multiplies both.
* A *start pattern* is five integers: the warm us that a voice's start work puts into the quads 0 .. 4 counted from the quad of its start tick (`StartPattern`). Quad index = tick count / 4 of the engine's own tick counter. Mono patterns from the table of section 1.2: offset 0: {156, 156, 0, 0, 156}; 128: {245, 0, 0, 156, 0}; 256: {245, 0, 156, 0, 0}; 384: {156, 245, 0, 0, 0}; Smear: {535, 0, 0, 0, 0}. The quad of a deferred frame is exactly `ticks / 4`: every join is a multiple of 128 samples = 4 ticks (`spectral.cpp:1134-1144`), and the frame is made in the control period at whose start `outPos_ >= join` (`spectral.cpp:1453-1456`, the run is cut at the join, `:1478-1479`), i.e. tick `join / 32` of the voice; the hop end (`endFirstHop`, `:1447-1449`) is at `512 - d` samples, again a multiple of 128. The pattern function lives next to `startSchedule` (`SpectralVoice::startPattern (offset, smears, channels)`), and a test counts the real frames and shapings per quad of a real voice against it (S3).
* The **budget** `B = kStartBudgetUs` warm us per quad (provisional 810, derivation below) and the **admission rule**: a voice asking in tick `t` (quad `Q = t / 4`) may start if for every `i` in 0 .. 4: `booked[Q + i] + pattern[i] <= B`, or if nothing at all is booked in the quads `Q .. Q + 4` (so a voice always makes progress, whatever `B` is).
* **First come first served**: requests are ordered by `key = (serial << 4) | (osc << 2) | u` (note serial of the engine's counter, `own_engine.cpp:815`; osc 0 .. 2; unison voice 0 .. 3). A voice may start only if no voice with a smaller key is waiting. Waiting voices are kept in a registry (insertion sorted by key, 128 entries; when it is full a request is granted).

**The protocol** (`StartLedger`, plain integers):
1. `beginTick()`, once per control period, called by the engine at `controlPos == 0` before the voices tick (`own_engine.cpp:1492`, next to `controlTick()`): increments the tick count; on a quad boundary clears the slot of the finished quad (ring of 8 quads: a start never books more than 4 ahead); drops registry entries not renewed in the previous period (their voice died); then walks the registry in key order and, for each entry whose pattern fits the rule above, books it and marks it granted; it stops at the first entry that does not fit (strict first come first served).
2. `request (key, pattern)`, called by `LibOsc::update` (once per control period for every waiting voice, and once at the note-on): an entry of that key that is granted is removed and the call returns true (the voice starts now); an entry that is not granted is renewed and the call returns false; a new key is granted at once if no entry with a smaller key waits and the pattern fits; otherwise it is inserted and the call returns false.
3. `cancelSerial (serial)`, called from `OwnEngine::killOverflowVoice` and from the steal path (`own_engine.cpp:775`, `:699`): removes the waiting entries of that note, and for the voices of that note that were already granted (the ledger keeps the last 32 grants: key, quad, pattern) gives back the bookings in quads after the current one (the strike of the current quad was really made at the note-on and stays booked). So a voice given up by the polyphony limit neither blocks the queue nor keeps budget for work it will not do.
4. With the ledger disabled (not tier low, or no `control=economy`) `request` returns true and touches nothing: **the code path of today**.

**What a waiting voice outputs: silence.** Its `SpectralVoice` is not started (`isActive()` false), so `LibOsc::renderType` skips it (`lib_osc.cpp:985-999`) and the oscillator adds exactly zero; the rest of the engine voice (envelopes, LFOs, the other oscillators, filters) runs as always. A "cheaper first frame" saves nothing: the strike of a voice is one shaping (67 us) plus one or two frames, the shaping is the part that cannot be cheapened (it needs the same spectrum). Stated consequence: when the voice starts, its amplitude envelope has advanced by the delay and the other oscillators of the engine voice lead the Spectral layer by it. The Vowel Freeze preset has `osc2` on (level 14) next to the Spectral `osc1` and an `env1.attack` of 800 ms (`content/presets/Pad/spectral_vowel_freeze.sbpreset`): a lead of 3 .. 20 ms against an 800 ms attack is not audible there (HYPOTHESIS for the other presets, listening test in S7).

**Single note, non-Spectral, tiers.** A single note finds the ledger empty: its voices start in the same control period as today. A mono sample with `U` = 1 .. 4: slot 0 is at most 156 + 245 + 245 + 156 = 802 <= 810 (the four offsets), so even a four voice unison note is not delayed. A stereo sample doubles the patterns: `U = 2` is 2 * 401 = 802 and fits, `U >= 3` may delay its last unison voice(s) by a block (section 6). Non-Spectral oscillators never call the ledger: not one instruction changes for them (the ledger lives in `spectral::Scratch`; `beginTick` is the only per-period call and does nothing while the registry is empty and the ring is zero). Tiers medium and high, and the XL profile (`high,voices=16,control=economy`, `shell-mpc/device_profile.h:187`): `spectralStart` is off, `request` returns true: bit-identical to today (the XL strike of 0.9 .. 1.2 blocks, `docs/MPC.md:354`, is outside this item's rows and stays as it is; `start=stagger` can be switched on there by the profile text).

**Determinism and block size.** The ledger advances once per control period in the engine's own order; at tier low events are on the 32 sample grid (`own_engine.cpp:1480,1608`), so a render does not depend on the host block size (tested with blocks of 32 .. 512 in S5) and repeats (the registry and ring are reset with the engine, `own_engine.cpp:559-566`). The random start fractions of the unison voices are drawn at the first call of the note in unison order, as today, and stored (`LibOsc::specStartFrac_[4]`), so the draws do not depend on how long a voice waits. The setting is only effective together with `kEconomyControl` (the grid): `spectralStartOf (quality)` is false without it, whatever `start=` says (S2).

**The budget `B`, derived.** Block = 128 / 44100 = 2902.5 us; the target of the item is 1.5 blocks = 4353.7 us. `base` = the largest measured block of the Spectral rows without a strike, the p99 of 1.79 ms (`before-414b46af.md`, p99 column; M2 replaces it): 1800 us. A strike block then costs `base + kappa * B`, with the cold factor `kappa` (HYPOTHESIS 3, `docs/MPC.md:354`; M1 decides). The largest budget that meets the target: `B_max = (4353.7 - 1800) / 3 = 851 us`. The smallest budget that lets a lone four voice unison note start whole: 802 us. Chosen: **`B = 810`**; strike block `1800 + 3 * 810 = 4230 us = 1.457 blocks`; every later quad is booked at most 810 too, so no carry block exceeds that either (the sums of the deferred frames of earlier chords are inside the same quad slots). Calibration rule (task S8): `B_new = floor ((4353.7 - W) / kappa_measured)` with `W` the measured worst block without a strike and `kappa_measured` the slope of M1; if all rows end below 1.3 blocks `B` may be raised to the value that makes the worst row 1.4 blocks, never beyond; if any row is above 1.5, `B` is lowered by the excess divided by `kappa`.

**The start delay, computed.** The rule above run for chords struck in one control period (a loop of 20 lines over the pattern table, kept as the unit test of S1; the voices get offsets `((s U + u) mod 4) * 128` for consecutive serials `s`, maximum over the four residues of the first serial; tick = 32 / 44100 = 0.7256 ms, at 48 kHz multiply by 0.919):

| spectral voices of the chord | notes x `U` | last voice starts after | grant ticks (voices sorted) |
|---|---|---|---|
| 1 .. 2 | 1 x 1 .. 2 | 0 ticks = **0 ms** | all at 0 |
| 4 | 1 x 4 | **0 ms** (slot 0 = 802 <= 810) | all at 0 |
| 3 | 3 x 1 | **0 ms** (slot 0 = 557 .. 646) | all at 0 |
| 6 | 3 x 2 | 4 ticks = **2.9 ms** | 0 0 0 0 4 4 |
| 8 | 4 x 2 (the three presets of section 1.2) | 8 ticks = **5.8 ms** | 0 0 0 0 4 4 8 8 |
| 8 | 8 x 1 | 8 .. 12 ticks = **5.8 .. 8.7 ms** | |
| 12 | 3 x 4 | 20 ticks = 14.5 ms | |
| 16 | 8 x 2 | 28 ticks = **20.3 ms** | 0 0 0 0 4 4 8 8 12 12 20 20 20 24 28 28 |

The question "3, 8 and 16 simultaneous notes": with the polyphony of the three presets (3 or 4, unison 2) a chord of 3 notes waits at most **2.9 ms** (its last note), a chord of 8 or 16 notes keeps at most 4 notes (Kill or Steal give up the rest). The notes that were granted at their own note-on and are given up later have really made their strike in the first quad (notes 0 and 1: 4 voices, 802 of 810 us), so the survivors (the last four notes) start in the next quad or later: by the same rule, with notes 0 and 1 granted at tick 0, the survivors start at ticks 4 4 4 4 8 8 12 12 = at most **8.7 ms** (without `cancelSerial` giving the future bookings back it would be 4 4 8 8 12 12 20 20 = 14.5 ms; the ledger-level arithmetic is in `sim`-style form in the unit test of S1, the engine figure is pinned by the Kill-burst test of S5). With a patch of polyphony 8 (the tier's cap: `qualityForTier (Tier::low)` has 8 voices, `quality.h:112`) and unison 1: 3 notes **0 ms**, 8 notes **5.8 .. 8.7 ms**, 16 notes (cut to 8 by Kill) HYPOTHESIS 8.7 .. 14.5 ms (the granted-then-given-up notes occupy the first quad; the S5 burst test with polyphony 8 decides); with unison 2: 3 notes 2.9 ms, 8 notes 20.3 ms, 16 notes at least that.

**Is it audible?** The mean wait of 8 voices is `(4 * 0 + 2 * 4 + 2 * 8) / 8 = 3` ticks = 2.2 ms, the latest voice of a four note chord waits 5.8 ms, and none of it is random: it is a fixed function of the chord. References (quoted from memory, to be confirmed before they go into release notes): latency of up to about 10 ms with jitter of about 1 ms is not noticed by players (Wessel and Wright 2002); the order of two onsets becomes audible from about 20 ms (Hirsh 1959); the onsets of the tones of one pianist's chord differ by 10 .. 30 ms by themselves (Goebl 2001). So up to 8 voices (<= 5.8 ms) is below what a player notices (HYPOTHESIS: a listening test on the Key 61 with the three presets, task S7, decides); the 20 ms of a 16 voice chord is at the edge and is the price of the 1.5 block target for a chord that size; the knob is `B`.
