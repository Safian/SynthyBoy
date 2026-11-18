# CPU review: the quality profiles, work that cannot be heard, and the shell's audio thread work (Key 61 first)

> **Implementation status of S1, S2, S4, S7 to S10, H2 (branch `work/opt-voice`, 2026-11-19).** Done and bit identical: S1 (V1), S2 (its parameter part, V7), S10 (V6), H2 (V10). Skipped: S4 and S8 (not bit identical: display and, after an edit, audio), S7 (tiny, needs delay line / rack / sign of zero conditions in code another worker changes), S9 (in `wt_osc.h`, another worker's file). The legato note of section 9 is confirmed as a bug (failing test `core/tests/test_own_voice_legato_env.cpp`).

Read only review of `feature/mpc-overlay` at 8e5cf113 (2026-11-18, after the round 12 merge of the three optimisation branches). No source file was changed, nothing was built or run, no device was touched. Every file:line below was re-opened in this tree.

The three earlier reviews (`CPU-REVIEW-OSC.md`, `CPU-REVIEW-FILTER-FX.md`, `CPU-REVIEW-CONTENT.md`) own the oscillators, the voice filters and effects, and the content engines. Their items are not repeated here. Where an item of this review touches one of theirs, it says so (for example S3 and FILTER-FX F10, the strip peaks).


## 1. Sources, method, constants

**Measured figures used (cited, not estimated).**

- Round 12 section shares of the block, Key 61 `low`, after the merge: `.hw-local/mpc-perf/round12/k61-r12-sections.csv` (column `pct_of_block`) for Lorenz Garden, Parallel Dusk Pad, Ring Mod Gamelan, Stretched Glass Choir, Triple Fifth Lead. Shepard Ascent is not in the round 12 section run; its shares are from round 10 (`round10/k61-breakdown/sections.csv`) and are marked so.
- Round 12 totals: `round12/k61-r12-tables.md` (median / p99 after the merge: Lorenz Garden 61.7 / 82.4, Stretched Glass Choir 51.0 / 85.7, Parallel Dusk Pad 41.1 / 69.7, Ring Mod Gamelan 42.2 / 69.5, Shepard Ascent 42.1 / 73.8, Triple Fifth Lead 52.1 / 75.2; budget p99 70 %).
- What-if runs: `round10/k61-breakdown/configs.csv` (profiles `low`, `low,unison=2`, `low,unison=1`, `low,voices=6`, `low,voices=4`, `low,fx=all`) and `variants.csv` (`poly4`, `uni1`, `fx_*_off`, `osc*lvl0`, `warpmorph0` ...). These are round 10 numbers (before the round 11 and 12 kernels). I use them as relative changes (the fraction of the block a switch removes) and scale them to the round 12 medians; the scaled figures are marked "scaled".
- The disassembly of the round 12 arm32 bench objects (`round12/bench-build/arm32/core/CMakeFiles/synthyboy_core.dir/engine-own/voice/voice.cpp.o`, `own_engine.cpp.o`, read with `objdump --triple=thumbv7-linux-gnueabihf`), and the dynamic symbol table of an arm32 plugin of 2026-11-14 (`.hw-local/mpc-perf/overlay-pkgs/ov10-play/VST/synthyboy-arm32.so`).

**Constants.** One block is 128 frames at 44.1 kHz = 2.902 ms = 4.666 M cycles at 1.608 GHz; **1 % of the block = 46.7 k cycles**. Four control periods of 32 samples per block. Cycle costs are estimates for the Cortex-A17 and are labelled "est.": a scalar float compare whose result is moved to the core flags (`vcmp`, `vmrs`) and used by a dependent conditional 7 to 11 cycles (the model of FILTER-FX section 1, which it calibrated against measured filter shares); a scalar VFP operation 1 to 2; a float libm call 80 to 200; a NEON Q operation about 2.

**What a "P ()" costs.** Every parameter read of a voice is `P (idx)` (`voice.h:185-189`): a load of the delta, a float compare with zero, a branch, a load of the base. With the compare model that is about 10 to 12 cycles (est.).

**The six presets** (read from `content/presets`; the routes named by `docs/params.json`):

| preset | voices at `low` | sub | filters | env 1 sustain / release | voice routes with a nonzero source in the bench |
|---|---|---|---|---|---|
| Lorenz Garden | 6 (preset polyphony) | on | F1 Allpasses, F2 on, series | 100 / 3200 ms | 4 random LFOs, velocity |
| Stretched Glass Choir | 8 | off | F1 on, F2 off | 90 / 2200 ms | random LFOs 1 to 3, LFO 1, velocity |
| Parallel Dusk Pad | 6 (preset polyphony) | off | both, parallel | 85 / 1800 ms | LFO 1, env 4, velocity |
| Ring Mod Gamelan | 8 (preset asks 16) | off | both, series | **0** / 320 ms, decay 1500 ms | env 2, env 3, velocity |
| Shepard Ascent | 4 (preset polyphony) | off | both, series | 100 / 3600 ms | LFO 1, LFO 2, random LFO 1, velocity |
| Triple Fifth Lead | 8 | off | F1 on, F2 off | 80 / 320 ms | env 2, velocity |

All macros are 0 and the bench sends no wheel or pressure, so the macro, mod wheel and poly pressure routes of these presets evaluate to exactly 0 in the bench (`mod_matrix.cpp:429` then skips them). All six have `reverb.on` (Triple Fifth Lead: `hyperdim.on`), which tier low masks, so every one of them runs a makeup meter (section 2, `fxMask`).


## 2. Every field of the profile, where it is read, and whether it removes the work it names

The Key 61 profile is `low` = `Profile {}` (`shell-mpc/device_profile.h:41-45`, `:188`): voices 8, unison 3, samples 0, effect mask of tier low, all four economy groups. `profileQuality` (`device_profile.h:159-169`) turns it into a `Quality`; `OwnEngine::setQuality` (`own_engine.cpp:302-311`) stores it in atomics; `process` copies them into `globals` once per block (`own_engine.cpp:1345-1349`, `:1378-1388`).

| field (Quality) | Key 61 value | read at | what it switches | removes the work? |
|---|---|---|---|---|
| `maxVoices` | 8 | `own_engine.cpp:304` -> `polyphonyLimit` `:567-571` (min with `master.polyphony`), `allocateVoice` `:618-633`, `killExcess` `:692-710` | the voice cap | Yes. Binds only Ring Mod Gamelan (asks 16), Stretched Glass Choir and Triple Fifth Lead (ask 8, equal). Lorenz Garden, Parallel Dusk Pad, Shepard Ascent ask 6, 6, 4: the cap does nothing for them. The pool stays 36 voices (`own_engine.h:85`), every loop over `voices` visits all 36 (item H2). |
| `maxUnison` | 3 (the profile; `quality.h:112` says 4 for tier low, but the MPC never uses that row) | `own_engine.cpp:305`, `:1345-1349` -> `voice.cpp:897` | lanes per oscillator | Yes. `WtOsc::update` builds `U` lanes (`wt_osc.h:236`, `:378-416`), `render` loops `max (curCount, prevCount)` lanes (`wt_osc.h:614`, `:634`). Lanes above the count are zeroed in `update` (`:418-422`) and the 16 lane `prev` copy (`:216-221`) runs every period (S9, trivial). Binds Triple Fifth Lead osc 1 (5 -> 3). |
| `oversampling` | 1 | `own_engine.cpp:306` (the sample tier only) and `economyOf` (`quality.h:102-105`) only when `economy < 0` | nothing else in the own engine | The MPC profile always sets `economy` explicitly (`device_profile.h:167`), so `oversampling` is only the low half of the sample tier. `tierLowCap` is false for every MPC profile (`own_engine.cpp:308`), so `lowUnisonCap` never applies on the MPC. The wavetable oversampling is not this field: it is the Warp group and `quality.oversample_4x` (item D7). |
| `interpolation` | linear | `own_engine.cpp:306` | sample tier 2 when spectral | The own engine reads it only as the sample tier. There is no spectral frame interpolation of wavetables in the own engine. |
| `fxMask` | tier low | `own_engine.cpp:1404` -> `fx_chain.cpp:396-400`, `:420`, pool `:250`, `:270` | reverb, flanger, Hyper / Dimension, Bode, Convolve bypassed | Yes for the effect itself: a masked module gets no `update` (`fx_chain.cpp:450-463` runs only `on[m]`) and no `process`. It stays allocated (prepared in `fx[i].prepare`, memory only). If it is on in the preset, a makeup meter and gain run in its place (next row). |
| `fxMakeup` | true | `own_engine.cpp:1439`, `:344` -> `fx_chain.cpp:420`, `:428-447`, `:642-653` | the level of a masked effect left as a gain | Runs only for an effect that is on in the preset AND masked (`fx_chain.cpp:420`), and is skipped with its rack on silence (`idleRack` skips bypass items, `fx_chain.cpp:738`). Per sample `ProgramMeter::accumulate` (`fx_makeup.cpp:141-161`, about 25 scalar operations); per period `endPeriod`, the model (cached on its keys, `fx_modules.cpp:89-112`) and `stepMakeup`. Measured (round 10, before the MK1 caches): `fx_reverb_off` takes Lorenz Garden 85.22 -> 84.57, Stretched Glass Choir 56.65 -> 56.08, Shepard Ascent 53.37 -> 52.75, Parallel Dusk Pad 78.12 -> 77.83, Ring Mod Gamelan 73.86 -> 73.59 (median, % of block). So the masked reverb's makeup costs 0.3 to 0.65 points. It is needed for the level; its removal is a design lever (D6). |
| `economy` Warp | on | `voice.cpp:940` -> `wt_osc.h:632`, `:545`, `:650` | 2x with 32 taps instead of 4x with 128; band limit 1.0 instead of 3.2 times the rate | Yes. `os` and `nt` are chosen per render (`wt_osc.h:632`); no 4x buffer is computed. The history buffer is sized for the 4x taps (`wt_osc.h:523`, memory only). None of the six uses a warp (OSC review table). |
| `economy` Morph | on | `voice.cpp:941` -> `wt_osc.h:535`, `:542`, `:458` | rebuild every 512 samples instead of 256, a half size transform, one shared group at tier low | Yes. The half size transform is real (`buildMorphCycle (..., tick.lowMorph, ...)`, `wt_osc.h:592`; OSC review 4b). The groups collapse to one (`wt_osc.h:542`). Stretched Glass Choir and Shepard Ascent morph. |
| `economy` Math | on | `voice.cpp:141`, `:942`, `:1088`, `:1306`; `own_engine.cpp:1440` | table / rational forms, held coefficients | Yes (FILTER-FX section 2). |
| `economy` Control | on | `own_engine.cpp:704`, `:955`, `:1045`, `:1385`, `:1397`, `:1442`, `:1445`; `voice.cpp:586`, `:607`, `:620` | events on the 32 sample grid; a stolen voice becomes a ghost ramp; unread envelopes / LFOs / random LFOs stepped every fourth period; an effect with mix 0 muted; a rack on silence skipped | Yes, each of these is real. It does **not** skip any modulation route and does not change the control period (32 everywhere, `dsp/util.h:20`). It slows an unread modulator to a quarter, it does not stop it (S4). A zero amount route still marks its source as read (S8). |
| `realtime`, `lazyTables` | true, true | `own_engine.cpp:385-396`, `:428` | helper threads, lazy tables | No per block cost. |
| sample tier (`samples=low`) | 0 | `own_engine.cpp:306` -> `voice.cpp:981` -> `lib_osc.cpp:459`, `:780`, `:808` -> `sample_voice.cpp:44-60`, `:155-158` | 4 taps instead of 8 / 16; one mip level (rounded) instead of a blend of two | Yes: fewer taps and one level read. It does not read a lower mip level than the pitch asks; it reads the nearest one instead of blending two. None of the six uses a sample oscillator. |

**Not a field, but a hole in the cap.** `quality.oversample_4x` is a preset parameter, not a profile field. `own_engine.cpp:1388` sets `globals.oversample` from it, `voice.cpp:939` copies it into every wavetable oscillator, and `wt_osc.h:618` then runs every oscillator on the oversampled path (at 2x on the Key 61, because the Warp group still applies, `wt_osc.h:632`). The tier does not cap it, although `quality.h:73-76` says a tier caps what a preset asks for. No factory preset sets it (checked: 0 of 160), but `serum-import/serum_import.cpp:636` sets it for every imported Serum preset with oversampling 2 or more. Item D7.


## 3. The loop nest of one block (Key 61), with the places of the findings

```
pluginProcessReplacing                          synthyboy_mpc.cpp:3013     per block
  audioMasterGetTime, setTransport              :3050-3065                 per block (one host call)
  eventInbox.pop loop                           :3068-3072                 per event (lock free)
  OwnEngine::process                            own_engine.cpp:1265        per block
    fills, strip peaks reset                    :1270-1286
    snapshot only when paramGen moved           :1290-1305                 per dirty block
    globals                                     :1306-1392
    period loop                                 :1398-1607                 4 per block
      controlTick                               :1098-1263                 per period
        global LFOs, random LFOs, macros        :1106-1138
        killExcess, updateLowestNote            :1142, :1149               36 voice visits each (H2)
        Voice::tick (each active voice)         voice.cpp:420              per period x voice
          envelopes (settings, advance)         voice.cpp:566-600          x 6           (S1)
          LFOs, random LFOs                     voice.cpp:605-640          x 10, x 4     (S4)
          sources, matrix                       voice.cpp:644-739
          amplitude, end of voice               voice.cpp:741-775                         (S6)
          oscillators (params, update, morph)   voice.cpp:785-992          x 3
          sub                                   voice.cpp:995-1022                        (S2)
          noise (skipped when off and settled)  voice.cpp:1024-1068
          filter settings                       voice.cpp:1070-1106        x 2           (FILTER-FX F8)
        global matrix, idle readout matrix      own_engine.cpp:1166-1262
      fx[0..2].update                           own_engine.cpp:1443-1447   x 3 racks     (FILTER-FX F11)
      mix buffers cleared                       :1517-1521
      Voice::render (each active voice)         voice.cpp:1122             per period x voice
        sources: osc render, halfSum, peakOf    voice.cpp:1189-1244        x osc         (S3)
        sub, noise (render, peakOf)             voice.cpp:1245-1322                       (S3)
        runFilter x 2 (+ peakOf)                voice.cpp:1325-1388                       (S3, FILTER-FX F9)
        extNext copy and zero                   voice.cpp:1133, :1390-1394                (S10)
        amplitude ramp, target mix              voice.cpp:1396-1448
      addGhosts                                 own_engine.cpp:1533
      strip sums, bus input peaks               :1534-1542                 per sample x 2 buses (S3)
      runBus x 2                                :1547-1583                 per sample   (S7)
      Main rack                                 :1584-1585                 (FILTER-FX)
      master loop: gain, routing, Direct, limit, master peak   :1589-1604  per sample   (H1)
    killExcess, active count                    :1615-1619
    publishStats, writeReadout                  :1620-1621, :1626-1879     per block    (S5)
  outputStage.process                           output_stage.h:47-90       per sample   (H1)
  outputPeak().note                             output_peak.h:18-32        per sample   (H1)
  spectrumRing.push                             spectrum.h:33-39           per sample
```


## 4. Work that cannot be heard: the early exits that exist

These are already right; nothing to do (file:line, re-checked):

- Free voices cost nothing: `own_engine.cpp:1530-1532` and `voice.cpp:1124-1125`; the tick loop `own_engine.cpp:1149-1151`. A voice ends when envelope 1 is done and both amplitude ends are 0 (`voice.cpp:764-774`).
- Stolen voices at tier low become a two value ramp ("ghost") instead of a 50 ms fade of the whole voice: `own_engine.cpp:704-707`, `:635-655`, `:658-681` (a ghost with silent channels is skipped, `:668`).
- An oscillator that is off: no routing reads, a deferred silent update, no render (`voice.cpp:827-840`, `:1191-1192`). Level 0 for the period: the update is deferred (`voice.cpp:957`, `wt_osc.h:448-469`) and the render returns at once (`wt_osc.h:609-610`). A silent lane only advances its phase (`wt_osc.h:636-646`).
- The update of an oscillator whose tick did not change returns after one `memcmp` (`wt_osc.h:225-226`).
- Noise off and settled: the whole noise block is skipped (`voice.cpp:1024-1027`); no render without gain (`:1259`).
- A filter that is off is not configured (`voice.cpp:1082-1083`). (Its wrapper still runs: FILTER-FX F9.)
- Targets that receive nothing are neither cleared nor mixed (`voice.cpp:1130-1163`, `:1401-1408`).
- Unread envelopes, LFOs and random LFOs at a quarter rate (`voice.cpp:586-591`, `:607-613`, `:620-625`).
- Matrix: a slot with amount 0 or output 0 adds nothing (`mod_matrix.cpp:425-430`); the matrix is compiled only on a dirty block (`own_engine.cpp:1292-1305`); the host's echo writes do not dirty the block (`synthyboy_mpc.cpp:2835-2836`).
- Effects: a chain with nothing on and nothing changed returns at once (`fx_chain.cpp:371-375`); a rack on silence is skipped (`fx_chain.cpp:774-778`); a module with mix 0 is muted and flushed once (`fx_module.h:149`, `:168-182`); the master volume, routing and latency plan are recomputed only when their inputs move (`own_engine.cpp:1405-1406`, `:1409`, `:1461`, `:1481`); a path delay of 0 is a no-op (`fx_latency.h:37-38`); a bus without bypassed effects skips the makeup sums (`own_engine.cpp:321-326`).
- Masked effects get no update and no process (`fx_chain.cpp:398`, `:450-463`).

**What is missing** (each is a finding below): the envelope settings cache never holds when anything is modulated (S1); an off sub still does its whole tick (S2); the mixer meters run when nothing shows them (S3); unread modulators are slowed, not stopped (S4); the display readout is written every block (S5); a voice at exactly zero amplitude that is not finished renders in full (S6); a bus that nothing feeds still runs its sample loops (S7); a zero amount slot marks its source as read (S8). There is no level based end of a voice (D8: a design lever, not a neutral item).


## 5. The shell's work on the audio thread

Per block, outside `engine->process` (`synthyboy_mpc.cpp:3013-3120`):

- **Host calls.** One `audioMasterGetTime` per block through `HostGate` (`:3050-3053`; the gate does two acquire loads, `:423`). Its cost is inside the host and cannot be read from this code. No other host call is made from the audio thread.
- **Events.** `eventInbox.pop` (`event_inbox.h`) is lock free on the audio side. The producer side takes a spin flag (`producers.test_and_set`); the only producer is `effProcessEvents` (`synthyboy_mpc.cpp:2416`), on the host's thread, so the audio thread never spins. Frame clamp and the All Sound Off insert are per event.
- **Parameters, Q-Links, Play proxies, pad pressure.** All arrive on host threads (`pluginSetParameter`, `:2866-2960`). A write equal to the mirror returns before the engine (`:2835-2836`), so echoes do not dirty a block. A real change bumps `paramGen` and costs one dirty block on the audio thread: the copy of 2637 relaxed atomics, `matrix.compile`, `noteTuning.compute` (`own_engine.cpp:1293-1334`), and invalidated voice caches; est. 10 to 20 k cycles, 0.2 to 0.4 % of that one block. A Q-Link sweep dirties many blocks but each one only by that much. Pad pressure: one atomic into the engine (`updateEnginePadPressure`); per voice a one pole and a flag test when the layer is on (`voice.cpp:714-738`), nothing when off. Program changes run `applyState` on the host's thread; the audio thread only sees dirty blocks.
- **Locks.** None on the audio thread. `programMutex` and `chunkMutex` are documented as never taken by it (`synthyboy_mpc.cpp:350`, `:373`), and I found no path from `processReplacing` to them. `PollGuard`'s mutex (`poll_guard.h:71`) is display thread only. `QRouter` has no lock.
- **Per sample after the engine.** `outputStage.process` (`output_stage.h:49-62`): per sample a gain compare, four `std::isfinite` (each a compare with `vmrs`), two multiplies; the limiter path adds a peak, a divide when over the ceiling, and its envelope. `outputPeak().note` (`output_peak.h:21-26`): two `std::fmax` per sample; `fmaxf` is an imported library function of the arm32 plugin (`objdump -T` of the 2026-11-14 plugin: `fmaxf`, `floorf`, `log10f`, `expf`, `powf`, `tanhf` are all imported), so each is a call. `spectrumRing.push` (`spectrum.h:33-39`): one add, multiply and store per sample, and one release store per block. All of this is outside the engine bench: the device block carries it on top of what the bench reports. Item H1.
- **Function statics.** `outputPeak()` and `startupTimes()` are function local statics; each call checks the guard (one acquire load). Three to four per block, negligible.
- **LFO live and spectrum feeds.** The audio thread does nothing for the LFO cursors except what the engine's `writeReadout` does every block (S5). The display thread polls `copyReadout` at `SYNTHYBOY_MPC_LFO_LIVE_HZ` = 10 Hz (`shell-mpc/CMakeLists.txt:80`) and the overlay at its frame rate. The spectrum: the ring above.
- **What the display thread shares with the audio thread.** The engine's `LiveValues` atomics (written every block, `own_engine.cpp:1758-1879`), `paramMirror` (host and UI writes), the spectrum ring, `outputPeak`'s atomics, `overlayBpm`. All relaxed or release / acquire, no locks, no copies of large buffers on the audio side.
- `shell-mpc/kit/display_thread.h` named in the task does not exist in this tree (`shell-mpc/kit/` has `binder.h`, `content_source.h`, `instance.h`, `kit.h`, `log.h`, `page_base.h`, `page_set.h`, `param_source.h`, `q_targets.h`, `synthyboy_adapter.h`, `synthyboy_params.h`). The display thread lives in `synthyboy_mpc.cpp:1394-` (`gLive.run` and the other guarded ticks).


## 6. Findings

### S1. The envelope settings cache is dropped whenever any key of the voice is modulated

- **Status (work/opt-voice, 6e0a0b0d): done as CPU-REVIEW-VOICE-MOD.md V1, bit identical.** The side note of section 9 is confirmed (`core/tests/test_own_voice_legato_env.cpp`, aea3d5ea); the fix is left to the owner (it changes output).

- **Where.** `voice.cpp:568` (`envFromCache` requires `numTouched == 0`), `:597` (`envCacheValid = numTouched == 0 && !fresh`), the settings lambda `:477-509` (out of line in the arm32 object: `Voice::tick ... EnvSettings` lambda at offset 0x48d4 of `voice.cpp.o`).
- **Trace.** `process` -> period loop -> `controlTick` -> `Voice::tick` -> envelope loop, 6 envelopes. `numTouched` counts every key the voice's matrix touched in the last period. In all six presets the velocity route to a filter key (or `filter1.mix`) is nonzero every period, so `numTouched > 0` always and all six envelopes rebuild their settings every period, although none of their keys is modulated (the env routes of Stretched Glass Choir and Ring Mod Gamelan come from macro 5 and macro 3, which are 0).
- **Loop level.** Per period x voice x 6 envelopes.
- **Cost (est.).** Per envelope about 10 `P ()` reads at 10 to 12 cycles, 4 more float compares, 5 multiplies, 6 stores and the 11 float cache store: about 160 to 200 cycles; 6 envelopes about 1.0 to 1.2 k cycles per voice period. 8 voices: 32 voice periods x 1.0 to 1.2 k = 32 to 38 k = 0.7 to 0.8 % of the block; allow plus or minus 40 % for the model: **0.4 to 1.0 % (Stretched Glass Choir, Ring Mod Gamelan, Triple Fifth Lead), 0.3 to 0.7 % (Lorenz Garden, Parallel Dusk Pad, 24 voice periods), 0.2 to 0.5 % (Shepard Ascent, 16)**. Bounded by the measured "voice tick: envelopes + LFOs" 3.1 to 4.5 %.
- **Consequence.** Bit identical: with no envelope key touched, every `P ()` of the settings returns the base value, the same as when the cache was built.
- **Confidence.** Mechanism high; size medium. **Risk.** Low.
- **Change sketch.** A table `envOfParam[idx]` (env number or -1) built once with the other key tables; after the matrix of the period, `envTouchedMask` = OR over `touched[0 .. numTouched)` of `1 << envOfParam`. Replace `numTouched == 0` in both places by `(envTouchedMask & (1 << e)) == 0` per envelope (or the whole mask zero). Add `sequence` to the cache key (see the side note in section 9: `legatoTo` changes the voice control factor without dropping this cache).

### S2. An off sub oscillator still does its whole tick

- **Status (73459fe8): done as the sub part of V7, bit identical.** The parameter part of the sub is kept (no key reads, no `round`); the silent tick is still built and deferred every period, which keeps the switch on mid note exact (the early exit of the sketch is not bit identical there). What stays per period: one acquire load of the table, three adds, a clamp and the copy of the silent tick.

- **Where.** `voice.cpp:995-1022`: about 12 `P ()` reads, `builtinWavetableIfReady` (an acquire load), `std::round`, the static table guard, the copy of a whole `OscTick` into `pendingTick` by `deferSilent` (`wt_osc.h:464`), for a sub that is off.
- **Trace.** `controlTick` -> `Voice::tick` -> sub block, per voice per period; the render is already skipped (`voice.cpp:1245`).
- **Measured.** "voice tick: sub" with the sub off (round 12): Stretched Glass Choir 0.59 %, Ring Mod Gamelan 0.49 %, Triple Fifth Lead 0.49 %, Parallel Dusk Pad 0.38 % of the block; Shepard Ascent 0.42 % (round 10). Lorenz Garden has the sub on (0.73 %, not a waste).
- **Saving.** Most of the section: **0.3 to 0.55 % on the five presets with the sub off**, 0 on Lorenz Garden. (The noise source already has exactly this early exit, `voice.cpp:1024-1027`.)
- **Consequence.** Bit identical while the sub stays off. When the sub is switched on during a held note, the old code replays the silent tick of the previous period before the live one (`deferSilent`); the new code has no silent tick of that period. The gain ramps from 0 in that period either way; only the pitch increment of the ramp's start may differ by a glide or bend step. Sound neutral, not bit identical in that one case.
- **Confidence.** High (measured section, simple mechanism). **Risk.** Low.
- **Change sketch.** As the noise: `if (!subCh.on && P (si.on) < 0.5f && subSettled) { pitchTapOn[3] = false; } else { ...as now...; subSettled = !subCh.on; }`, and in the period the sub comes on, run the block twice (first with level 0, as a silent tick, then live), which reproduces the old replay except for the pitch.

### S3. The mixer meters run on every voice, source and period, also when no page shows them

- **Status: not part of this branch** (the meters belong to the shell / peak work).

- **Where.** `peakOf` (`voice.cpp:1112-1119`) at `:1238-1242` (each oscillator), `:1252-1256` (sub), `:1316-1320` (noise), `:1346-1350` (each filter, also an off one); the bus input peaks `own_engine.cpp:1536-1542`; the master peak `:1603`; the strip sums `:1534-1535`. `mo.srcSq` is always set (`own_engine.cpp:1529`), so the voice always measures.
- **Disassembly.** `Voice::render`'s `runFilter` lambda (offset 0x429e of `voice.cpp.o`): `vldmia`, `vabs.f32`, `vcmp.f32`, `vmrs APSR_nzcv`, `it mi`, `vmovmi.f32`, `cmp`, `bne`: a scalar loop with the flag transfer in the loop carried chain.
- **Who reads them.** The source strips 0 to 6 only `overlay/mixer_ui.h:187`; the bus strips 7, 8 only `overlay/fx_ui.h:763`; the master strip 9 `overlay/master_ui.h:96` and `fx_ui.h:763`. Without the overlay, or on any other page, nobody.
- **Cost (est., 7 to 11 cycles per sample).** Peaked samples per block = voices x 4 periods x strips x 32. Lorenz Garden 6 x 4 x 6 x 32 = 4608 -> 32 to 51 k = **0.7 to 1.1 %**; Triple Fifth Lead 8 x 4 x 5 x 32 = 5120 -> **0.8 to 1.2 %**; Stretched Glass Choir and Ring Mod Gamelan 4096 -> **0.6 to 1.0 %**; Parallel Dusk Pad 3072 -> **0.5 to 0.7 %**; Shepard Ascent 2048 -> **0.3 to 0.5 %**. Bus peaks 2 x 2 x 128 = 512 compares -> 0.08 to 0.12 %; master 256 -> 0.04 to 0.06 %.
- **Relation to FILTER-FX F10.** F10 makes the same loops NEON (its saving is the same arithmetic base, 0.3 to 1.1 %). S3 removes them when no reader wants them. Done after F10, S3 still saves the vector residue, about a quarter: 0.1 to 0.3 %. Done instead of F10, it saves the whole figure whenever the Mixer page is not open, and F10 then only matters while it is.
- **Consequence.** Audio bit identical (the meters never feed the sound: `srcSq` and `stripBlockPeak` are read only by `writeReadout`). The `srcLevel` values (`voice.cpp:1235`, `:1251`, `:1315`, `:1345`) are read by the matrix and stay.
- **Confidence.** High. **Risk.** Low.
- **Change sketch.** `IEngine::setReadoutDemand (uint32_t bits)` (any thread, one atomic): bit 0 source strips, bit 1 bus strips, bit 2 master, bit 3 per voice LFO / envelope dots, bit 4 sources, bit 5 targets, bit 6 grains. The overlay's `Controller` sets the bits of the current view on every view change (and 0 when the overlay is hidden); the LFO live thread sets bit 3 for its four LFO phases (only `lfoReadPos`, see S4). In `process`: `mo.srcSq = (demand & 1) ? stripSq : nullptr` (the voice already tests `out.srcSq != nullptr`), and guard the bus and master peak loops by their bits.

### S4. Unread modulators are slowed to a quarter, not stopped

- **Status: skipped, not bit identical.** The shape value of an unread LFO is read: the display shows the sources of the newest voice (`writeReadout`), and the smoothing state carries every value into the next; an LFO that only advanced its phase shows another value when its voice is or becomes the newest, and after an edit that makes it read, the audio differs.

- **Where.** `voice.cpp:605-615` (LFOs), `:616-640` (random LFOs), `:584-594` (envelopes); `Voice::lfoValue` `voice.cpp:299-375` (about 16 `P ()` reads, the rate, the core advance, one or two shape evaluations).
- **Trace.** `Voice::tick`, per period x voice x 10 LFOs / 4 random LFOs / 5 envelopes; an unread one runs every fourth period with four periods of time.
- **Who reads an unread modulator.** No slot (that is what "unread" means, `usedSources`). The display: the LFO cursors read only `lfoReadPos` (`voice.h:175`, the phase, set in `lfoValue` before the shape), the envelope dots read the envelope stage and position; the source meters of the display read `src[]` of the newest voice (`own_engine.cpp:1783-1797`).
- **Count in the six.** Unread LFOs: Lorenz Garden 10, Stretched Glass Choir 9, Parallel Dusk Pad 9, Ring Mod Gamelan 10, Shepard Ascent 8, Triple Fifth Lead 10; unread random LFOs: 0, 1, 4, 4, 3, 4; unread envelopes 2 to 6: 5, 5, 4, 3, 5, 4.
- **Cost (est.).** `lfoValue` 300 to 600 cycles; the shape part (the `P ()` reads of shape, swing, direction, stereo, smooth, fade, and the lane evaluations) about half of it. A quarter rate unread LFO costs 75 to 150 per voice period, of which the shape is 40 to 75. Ten unread LFOs at a quarter: the shape part 400 to 750 cycles per voice period. Unread random LFOs add little that can go (their generator must keep stepping, see the sketch): 0 to 100. Ring Mod Gamelan, Triple Fifth Lead (32 voice periods): 13 to 30 k = **0.3 to 0.65 %**; Stretched Glass Choir **0.3 to 0.55 %**; Lorenz Garden, Parallel Dusk Pad (24): **0.2 to 0.45 %**; Shepard Ascent (16): **0.15 to 0.3 %**.
- **Consequence.** Bit identical audio: nothing audible reads these values. The display keeps its phases (the phase is still advanced). When a slot starts reading the modulator (a matrix edit), its first value comes from a phase that was stepped at a quarter rate, as today.
- **Confidence.** Medium low (the split of `lfoValue` cost is estimated). **Risk.** Low to medium (the split must keep the phase, the fade age and the random state exactly as now).
- **Change sketch.** Split `lfoValue` into `lfoAdvance` (rate, core advance, age, `lfoReadPos`) and `lfoShapeValue`. For an unread LFO at its quarter step call `lfoAdvance` only, and `lfoShapeValue` only for the newest voice when demand bit 4 is set. A random LFO that no slot reads: keep stepping its generator (the sequence must stay the one a later read would see), which is most of its cost; only its key reads could be cached. Envelopes: keep as now (their advance is cheap and their stage is the display's).

### S5. The display readout is written every block

- **Status: not part of this branch.**

- **Where.** `own_engine.cpp:1620-1621` -> `publishStats` `:1626-1677`, `writeReadout` `:1758-1879`.
- **What it does per block (est.).** 10 strip levels with `log10f` and the release (`:1762-1773`, the `exp` once): 1 to 1.5 k; the newest voices by serial, 8 passes over 36 voices (`:1805-1815`): about 2 k; per found voice 3 read positions, the sub phase, the step, 10 `lfoReadout` (each a `floorf` call, an import of the plugin), 6 `envReadoutFine` (a division each): about 3 k; 64 sources; the newest voice's envelopes; up to 96 targets (`:1862-1876`; the overlay subscribes the targets of all 24 views at its start, `synthyboy_mpc.cpp:1290-1299`, `overlay/controller.h:133-146`), each a `P ()` or a modulated value: 1.5 to 6 k; `publishStats` about 1 k. Total about 8 to 14 k cycles = **0.17 to 0.3 % of every block**. The bench sets no targets, so it shows the lower end.
- **Readers.** The LFO live thread at 10 Hz, the overlay at its frame rate. The audio thread writes at 344 blocks a second.
- **Saving.** Writing every fourth block (86 Hz, above both readers) saves three quarters: **0.12 to 0.22 %**. With S3's demand bits, the parts nobody shows are skipped too.
- **Consequence.** Audio bit identical. The meters stay correct if `stripBlockPeak` is not reset between the written blocks (keep the max over the four) and the release uses four blocks of time.
- **Confidence.** Medium. **Risk.** Low.
- **Change sketch.** `if (++readoutTick & 3) { accumulate peaks; return; }` at the top of the two calls; `stripBlockPeak` reset only after a write; `lastBlockFrames` summed.

### S6. A voice at exactly zero amplitude that has not finished renders in full

- **Status: not part of this branch.**

- **Where.** `voice.cpp:764-774`: a voice ends only when envelope 1 is `done` (or a kill fade ends). Envelope 1 in sustain with sustain 0, or in release from a value of 0 (`envelope.h:162-173`), gives `ampPrev = ampCur = 0` for as long as the key is held, or for the whole release time, while the oscillators, filters and mix run.
- **Which preset.** Ring Mod Gamelan: sustain 0, decay 1500 ms with curve -3. The amplitude (the square of the envelope) is below -60 dB after 84 % of the decay (1.26 s) and exactly 0 from 1.5 s on; a key held longer, and the following 320 ms release from 0, are exact silence at full cost. The other five have a sustain above 0.
- **Bench.** The bench strikes the chord again every second (`bench/tier_bench.cpp:385-405`), so it never reaches this state: **0 in the measured figures**. In playing, a held chord of Ring Mod Gamelan past 1.5 s costs everything for nothing: per voice about (42.2 - fixed part) / 8, est. 4 to 5 points of the block per such voice at round 12 (scaled from `configs.csv` `voices=6` against `low`: 2 voices = 16.3 points at round 10 out of 74.45, i.e. 21.9 % of the median; 21.9 % x 42.2 / 2 = 4.6 per voice).
- **Consequence.** Bit identical when skipped under these conditions: every source the voice routes goes through envelope 1 (no "N" target, `voice.cpp:1129`); `declickPos == 0`; envelope 1 in sustain with the modulated sustain exactly 0 and no slot touching an envelope 1 key, or in release with `releaseFrom == 0`; `legatoInv[0]` false (`voice.cpp:258-261`: a legato note could restart envelope 1 from its value); no slot reads the oscillator or filter level sources (`kSrcOscA` .. `kSrcFilter2`, they would freeze). The voice's own output is then exactly +0; the state it skips is never heard again before the voice ends or is restarted (`Voice::start` resets it, `voice.cpp:158-246`).
- **Confidence.** High for the mechanism; the real play saving depends on how the preset is played. **Risk.** Medium (the conditions must be complete).
- **Change sketch.** In `tick` after the amplitude block: compute `silentHold` from the conditions; if set, keep the envelopes, LFOs and matrix (they are the sources of the global slots and the display), skip the oscillator, sub, noise and filter sections, and let `render` return after setting `lastBus*` and `outL / outR` to 0. Clear `silentHold` the moment any condition fails, and on that period run the full tick (the oscillators restart from their held state, which nothing heard in between).

### S7. A bus that nothing feeds still runs its per sample loops

- **Status: skipped.** Est. 0.06 %; an exact skip needs the bus delay lines (their state), the bus rack tails, the ghosts and the sign of zero of Main covered, and it sits in the bus / peak code of `own_engine.cpp` that the shell and peak work changes now.

- **Where.** `own_engine.cpp:1547-1583` (`runBus`: the gain lerp loop, the add to the destination), `:1536-1542` (bus input peak, see S3).
- **Six presets.** No send to Bus 1 or Bus 2 anywhere (all `*.bus1_send`, `*.bus2_send` 0); the bus racks already take their early exit (FILTER-FX F11 measured that).
- **Cost (est.).** Per bus per period 32 x about 8 cycles (lerp, two multiplies, two stores) + 64 adds: 350 to 400 cycles; 2 buses x 4 periods: about 3 k = **0.06 to 0.07 %**.
- **Consequence.** Bit identical except that a `-0` in Main could become `+0` (inaudible).
- **Confidence.** Medium. **Risk.** Very low.
- **Change sketch.** The voices set `busFed[b]` when they touch target b (`usedT`), ghosts too; skip `runBus (b)` when `!busFed[b]` and the bus rack is idle (`!fx[b].active ()` or quiet) and `busGainPrev == busGain`.

### S8. A slot with amount 0 marks its source as read

- **Status: skipped, not bit identical.** A source behind a zero amount slot that is left out of the mask is decimated at tier low: the display of the newest voice's sources changes at once, and its phase and smoothing state (4 steps against one of 4 times the length) differ when an edit gives the slot an amount.

- **Where.** `mod_matrix.cpp:302-303` adds the source bits of every compiled slot to `srcMask`, also of a slot whose amount is 0 (it is compiled at `:276-301` and skipped at evaluation, `:425`). So an LFO behind a zero amount slot runs every period, not at the quarter rate.
- **Six presets.** No slot with amount 0: **0**. Matters for user patches with a parked route.
- **Consequence.** Bit identical if the bits are left out only for slots whose amount is 0 and that no other slot modulates (`amountDest`).
- **Confidence.** High. **Risk.** Low.
- **Change sketch.** In `compile`, after the loop, OR the bits only of slots with `c.amount != 0` or with an incoming amount slot.

### S9, S10. Small fixed copies (each about 0.1 % or less)

- **Status: S10 done in 738dc125 (with V6), bit identical. S9 skipped here:** it is in `osc/wt_osc.h`, the wavetable oscillator, which another worker changes in this round (hand it to that work).

- **S9.** `WtOsc::update` copies all 16 lanes of `curInc`, `curGainL`, `curGainR` into `prev` every period before its early return (`wt_osc.h:216-221`), and zeroes the lanes above the count when it does not return (`:418-422`). Lanes at or above `max (curCount, prevCount)` are never read by `render` (`wt_osc.h:614`, `:634`). Copying `max (curCount, prevCount)` lanes is bit identical. Saving est. 30 to 40 cycles x 3 oscillators x 32 voice periods = 3 to 4 k = **0.06 to 0.08 %**. (OSC F6 kept this copy on purpose as "not the cost"; I agree it is small.)
- **S10.** `float extNext[4][kControlPeriod] = {}` (`voice.cpp:1133`) zeroes 128 floats per voice period, then `:1390-1394` copies them. Only the entries whose source is off need the zero. Saving est. 32 voice periods x 100 to 150 cycles = **0.07 to 0.1 %**. Bit identical.

### H1. The output tail: the engine's master loop, the shell's stage and peak, all scalar

- **Status: not part of this branch** (shell / master work).

- **Where.** `own_engine.cpp:1589-1604` (per sample: `smoothedGain.next`, the loop invariant test `routeSin != 0.0f`, the Direct lerp, two `outputLimit` each with a `fabs` compare and a NaN compare, the master peak with two `max`); `output_stage.h:49-62` (a gain compare, four `std::isfinite`, two multiplies); `output_peak.h:21-26` (two `fmaxf` calls).
- **Cost (est.).** Engine loop about 7 flag transfers per sample: 128 x 65 to 90 = 8 to 11.5 k (0.17 to 0.25 %). Shell stage 128 x 40 to 60 = 5 to 8 k (0.11 to 0.17 %). Peak 256 calls x 15 to 25 = 4 to 6.5 k (0.08 to 0.14 %). Together **0.36 to 0.56 % per block**, of which the shell part (0.2 to 0.3 %) is not in any bench figure.
- **Saving.** Hoist `routeSin != 0` (loop invariant); a NEON body for the common case (all `|y| <= 1.8` and finite, tested with integer compares on the bit patterns, fall back to the scalar loop for the block otherwise); the master peak with `vmaxq_f32` (FILTER-FX F10 lists this one loop; the rest is new); in the shell, a NEON finite test over the block and one multiply pass, and the output peak with `vmaxq_f32 (vabsq_f32)` (the input is already finite, so `fmax` and `max` agree). **0.25 to 0.45 %** together.
- **Consequence.** Bit identical (the same operations per sample; the vector test only chooses the path).
- **Confidence.** Medium. **Risk.** Low.

### H2. Thirty six voice objects are visited many times per period

- **Status (9cec1ec1): done as CPU-REVIEW-VOICE-MOD.md V10, bit identical.**

- **Where.** The pool is `kMaxVoices + 4` = 36 (`own_engine.h:85`) whatever the cap. Each period: `controlTick` visits all 36 for `sustainPending` (`own_engine.cpp:1139-1141`), `killExcess` (`:696-697`), `updateLowestNote` (`:686-688`), the tick loop (`:1149-1151`), the newest voice (`:1161-1163`); `process` the render loop (`:1530-1532`). Per block: `killExcess`, the active count, `publishStats`, `writeReadout`'s newest search and its 8 passes (`:1775-1777`, `:1805-1815`). About 1.2 to 1.4 k visits per block, each a load of `active` from a separate heap object (an inactive voice's line is cold).
- **Cost (est.).** 5 to 15 cycles per visit: 6 to 20 k = **0.13 to 0.4 %**. Low confidence on the cycle figure (cache behaviour).
- **Saving.** Keep a compact list (or a 64 bit mask) of active voice indices, updated in `start`, `kill` and at the end of a voice; iterate it. Most of the figure. Bit identical if the list is kept in pool order (the allocation and the order of summing into the mix stay the same).
- **Risk.** Low to medium (every place that ends a voice must update the list).


## 7. Already well optimised (keep as is)

- The profile is resolved once per process, never on the audio thread (`device_profile.h:266-306`); `setQuality` is seven relaxed stores (`own_engine.cpp:302-311`).
- Unison: only the lanes of the count are built and rendered (`wt_osc.h:236-417`, `:634`); silent lanes only advance their phase (`:636-646`); the per lane band level is cached on its ratio (`:652-654`).
- Warp 2x and the half morph are real reductions (`wt_osc.h:632`, `:535-542`).
- The silent oscillator deferral (`wt_osc.h:448-469`) and the tick `memcmp` (`:225`).
- Ghost kills at tier low (`own_engine.cpp:704-707`, `:635-681`) instead of 50 ms fades of whole voices.
- The event grid at tier low: blocks are never cut by events (`own_engine.cpp:1502-1507`).
- The FX early exits and the makeup caches (`fx_chain.cpp:371`, `:774`; `fx_module.h:149`, `:168`; `fx_modules.cpp:89-112`).
- The master volume, routing and latency plan only on change (`own_engine.cpp:1405-1499`).
- The matrix: zero skips at evaluation (`mod_matrix.cpp:425-430`), compile only on dirty blocks.
- The shell: echo writes do not reach the engine (`synthyboy_mpc.cpp:2835`); the audio thread takes no lock; one host call per block; the spectrum feed is one store per sample into a preallocated ring.


## 8. Verdicts on the hypotheses of the thin review (`agy-g80-review-profile-deep`)

| # | claim | verdict | why |
|---|---|---|---|
| 1.1 | makeup gain of a bypassed effect is "redundant gain staging" | refuted | The gain is the purpose of `fxMakeup` (the level of the full quality). It runs only for an effect that is on in the preset and masked (`fx_chain.cpp:420`). Its cost is measured: 0.3 to 0.65 points for the masked reverb (round 10 `fx_reverb_off`). Removing it changes the level: design lever D6. |
| 1.2 | each economy flag "reduces work significantly" | partly | True for Warp, Morph, Math (the six use no warp, so Warp saves them 0). Control does not skip modulation routes and does not change the control period; it slows unread modulators to a quarter (S4) and mutes mix 0 effects. |
| 2.1 | "force effect skip for mix = 0 regardless of kEconomyControl", 2 to 5 % | refuted for the MPC | Both MPC profiles have `control=economy` (`device_profile.h:187-188`), so the skip is already on (`own_engine.cpp:1442`). The reason it is in the economy group: a muted module is flushed and loses its tail when the mix opens again (`fx_module.h:158-166`), which is not bit identical. Saving on the MPC: 0. |
| 2.2 | ended voices keep computing, 5 to 15 % (item 1 of its table) | refuted | A voice whose envelope 1 is done is freed at once (`voice.cpp:764-774`) and a free voice costs nothing (`own_engine.cpp:1530-1532`, `voice.cpp:1124`). Stolen voices are ghosts at tier low. Saving in the bench: 0. The real residue is different and narrower: a voice at exactly zero amplitude that is not done (S6), and the quiet end of a release (D8). |
| 3.1 | `eventInbox` synchronisation adds audio thread jitter | refuted | The pop is lock free (`event_inbox.h`, `pop`); the spin flag is on the producer side, whose only caller is `effProcessEvents` on the host's thread (`synthyboy_mpc.cpp:2416`). |
| 3.2 | the meters are "minor per block overhead", `std::fill` of `stripBlockPeak` | partly | The fill is 10 floats, negligible. It missed the real meter cost: `peakOf` per source per voice per period, scalar with a flag transfer per sample, 0.3 to 1.2 % (S3, FILTER-FX F10). |
| table 3 | "disable makeup gain for bypassed but inaudible effects", 1 to 3 % | partly, 0 for the six | A masked effect that is on with mix 0 still runs its meter although its model gives a gain of 1. None of the six has that (reverb mixes 20 to 32 %). Elsewhere it is the 0.3 to 0.65 points above, only in that corner case. |


## 9. Profile limits for the Key 61 as design levers (each is audible)

Where the six stand after round 12 (p99 against the 70 % budget): Lorenz Garden 82.4 (needs 12.4 points), Stretched Glass Choir 85.7 (15.7), Triple Fifth Lead 75.2 (5.2), Shepard Ascent 73.8 (3.8), Parallel Dusk Pad 69.7, Ring Mod Gamelan 69.5 (inside, barely). The sound neutral items of this review add up to about 1.5 to 4 points of the median; they do not close the gap of Lorenz Garden and Stretched Glass Choir alone. (OSC F1, the staggered morph rebuild, is the largest p99 item for Stretched Glass Choir and Shepard Ascent: minus 8 to 19 points of p99, not bit identical, owner's decision.)

`synthyboy_profile_compare` measures steps up from `low` (`bench/profile_compare.cpp:16`); none of the steps down below was run with it. `docs/MPC.md:524` has the only measured sound statements: Control alone with notes on the grid and no stolen voice gives the same samples; the unison cap keeps the level within 0.5 dB on a plain saw and changes the waveform; `fx=all` moves the level with the effects' mix.

- **D1. Control period 64 samples at tier low.** The voice tick sections are per period by construction ("calls" in the CSV = voices x 4). Their round 12 sum plus "fx: update": Lorenz Garden 16.8 + 1.6 = 18.5 %, Stretched Glass Choir 17.5 + 1.6 = 19.2 %, Triple Fifth Lead 15.7 + 1.3 = 16.9 %, Ring Mod Gamelan 13.9 + 1.5 = 15.4 %, Parallel Dusk Pad 12.0 + 1.3 = 13.2 %, Shepard Ascent 10.8 + 1.5 = 12.3 % (round 10). Half the periods saves about half: **est. 5 to 10 points of the median on all six**, the largest lever here. Sound: modulation and envelope corners every 1.45 ms instead of 0.73 ms (the values are ramped across the period, so it is a smoother, slightly later modulation, not steps); events on a 1.45 ms grid; fast attacks of envelope 2 to 6 rounder. Not measured. Risk high: `kControlPeriod` is a compile time constant in array sizes and ramps across the engine, and the envelope delay chunk is defined as four steps of 32 samples (`envelope.h:81-82`, document 03 1.3a). A cheaper partial form: tick every voice every period but rebuild the oscillator parameters and filter settings every second period (est. 2 to 4 points; also not measured).
- **D2. `voices=6`.** Measured what-if (round 10 `configs.csv`): Stretched Glass Choir median 56.78 -> 46.56 (-18 %), p99 101.31 -> 73.11 (-28 %); Ring Mod Gamelan 74.45 -> 58.17 (-22 %), p99 93.95 -> 77.80 (-17 %); Triple Fifth Lead 72.99 -> 58.83 (-19 %), p99 93.59 -> 79.03 (-16 %). Scaled to round 12: **about 9 to 10 points of the median on these three, 11 to 24 points of p99**; 0 on Lorenz Garden, Parallel Dusk Pad, Shepard Ascent (they ask 6, 6, 4). Sound: a 7th and 8th note steal (a ghost ramp at tier low); long pad tails are cut on chord changes. Not measured with `profile_compare`. Risk low (one string).
- **D3. Morph rebuild every 1024 samples at tier low ("morph=quarter", a new step of the Morph group).** "osc: morph cycle build" is 4.51 % of the block for Stretched Glass Choir (round 12) and 5.79 % for Shepard Ascent (round 10, before F2 to F4). Half the rebuilds: **est. 2 to 3 points of the median on these two, more at p99** (the rebuilds are the p99 spikes, OSC F1). 0 on the other four. Sound: the morph moves in 23 ms steps with the existing cross fade instead of 11.6 ms; both presets move the morph with slow LFOs (0.085 Hz, random LFO). Not measured. Risk low (`wt_osc.h:458`, `:535` take the period from the group).
- **D4. `unison=2`.** Measured (round 10): Stretched Glass Choir median -4 % (56.78 -> 54.49), p99 -14 % (101.31 -> 86.76); Lorenz Garden -1.6 %; Triple Fifth Lead -2.2 %; the others about 0. Scaled: **about 1 to 2 points of the median, Stretched Glass Choir about 12 points of p99**. Sound: the three lane oscillators lose the centre voice or one side voice: thinner, a different spread (the 0.5 dB / "another waveform" statement of `MPC.md:524` is for the cap in general). Risk low.
- **D5. Chorus masked at tier low (with its makeup).** "fx: chorus" is 4.84 to 4.93 % of the block (round 12) in Lorenz Garden, Stretched Glass Choir, Parallel Dusk Pad, Triple Fifth Lead and Shepard Ascent. **About 4.5 points on these five** (minus the makeup meter). Sound: high, the chorus is the width of these pads. Risk low.
- **D6. `fxMakeup` off at tier low.** Measured upper bound (round 10, before MK1): 0.3 to 0.65 points (the `fx_reverb_off` variants). Sound: the level of every preset with a masked effect falls back to the pre-makeup level (the reverb's share, about 1 to 3 dB by `TIER-LEVEL-COMPENSATION.md`). Not worth it.
- **D7. Cap `quality.oversample_4x` at tier low.** 0 for the six and for all factory presets. For an imported Serum preset with oversampling, every wavetable oscillator runs the 2x path (two reads and a 32 tap decimation per lane sample instead of one read): est. 1 to 3 times the "wavetable read" section on top (Lorenz Garden's read is 9 % of the block). Sound: those imports alias a little more above 10 kHz, as every other preset at tier low. A guard more than a lever; it matches the contract of `quality.h:73-76`.
- **D8. A level based end of a voice (for example below -90 dB of its peak, envelope 1 in release).** 0 in the bench (it retriggers and steals). In playing, with envelope curve -3 the last 2 % (-100 dB) to 6 % (-80 dB) of a release is below those levels; at the voice cap the quietest released voice is stolen first anyway (`own_engine.cpp:593-598`). Small. Not bit identical. Sound: none at -90 dB, but a filter that rings or a long delay is cut in the voice.

Order by saving per audible cost: **D1** (largest, small change of feel, but the riskiest work), **D3** (two presets, very small audible cost), **D2** (large on three presets, steals are audible), **D4** (one preset's p99, thinner sound), **D7** (guard, imports only), **D6** and **D5** (small or very audible), **D8** (small).

**Side note (correctness, not CPU, unverified).** `envCacheValid` is not dropped by `legatoTo` (`voice.cpp:248-267` drops only `vcCacheValid`), and the cached settings carry `vc.envFactor` (`voice.cpp:504-508`). In a legato with voice control envelope scaling and no modulated key, the envelopes may keep the factor of the previous step. S1's change should key the cache on `sequence` as well.


## 10. Ranked tables

Rank = (saving x certainty) / risk. Savings are percent of the 2.902 ms block for the six presets (est. unless "measured").

**Sound neutral (bit identical unless noted)**

| rank | ID | saving (% of block) | certainty | risk | change in one line |
|---|---|---|---|---|---|
| 1 | S3 | 0.3 to 1.2 (Triple Fifth 0.8 to 1.2, Lorenz 0.7 to 1.1, Stretched / Ring Mod 0.6 to 1.0, Dusk 0.5 to 0.7, Shepard 0.3 to 0.5); 0.1 to 0.3 if FILTER-FX F10 lands first | high | low | readout demand bits from the overlay; `srcSq = nullptr` and no bus / master peaks when no view shows them |
| 2 | S2 | 0.3 to 0.55 measured section (Stretched, Ring Mod, Triple Fifth, Dusk, Shepard); 0 Lorenz | high | low | early exit of the sub tick when off and settled, as the noise has (sound neutral, bit identical except a sub switched on mid note) |
| 3 | S1 | 0.4 to 1.0 (8 voices), 0.3 to 0.7 (6), 0.2 to 0.5 (4) | medium | low | keep the envelope settings cache unless an envelope key is touched; key it on `sequence` too |
| 4 | H1 | 0.25 to 0.45 (shell part not in any bench) | medium | low | hoist `routeSin`, NEON master loop with a scalar fallback, NEON finite test and peak in the shell stage |
| 5 | S4 | 0.15 to 0.65 | medium low | low to medium | split `lfoValue` into phase advance and shape; unread LFOs advance only, shape only when shown |
| 6 | S5 | 0.12 to 0.22 | medium | low | write the readout every fourth block, peaks accumulated |
| 7 | H2 | 0.13 to 0.4 | low | low to medium | iterate a list of active voices in pool order, not the 36 pool |
| 8 | S10 | 0.07 to 0.1 | medium | very low | zero `extNext` only for the sources that are off |
| 9 | S9 | 0.06 to 0.08 | medium | very low | copy `prev` lanes only up to `max (curCount, prevCount)` |
| 10 | S7 | 0.06 to 0.07 | medium | very low | skip `runBus` when nothing fed the bus and its rack is idle (+0 for -0 only) |
| 11 | S6 | 0 in the bench; about 4 to 5 per held voice of Ring Mod Gamelan past 1.5 s in playing | high (mechanism) | medium | skip the oscillators, filters and render of a voice whose envelope 1 holds exactly 0 and routes everything through it |
| 12 | S8 | 0 for the six | high | low | leave zero amount slots out of the source mask |

**Audible design levers (the owner decides)**

| order | ID | saving | measured? | sound | risk | change in one line |
|---|---|---|---|---|---|---|
| 1 | D1 | 5 to 10 points of the median, all six (est. from measured tick sections) | no | modulation corners every 1.45 ms, event grid 1.45 ms | high | control period 64 at tier low |
| 2 | D3 | 2 to 3 points median, more at p99, Stretched and Shepard | no | morph steps 23 ms instead of 11.6 ms | low | morph rebuild every 1024 samples at tier low |
| 3 | D2 | Stretched, Ring Mod, Triple Fifth about 9 to 10 points median, 11 to 24 p99 (scaled); 0 on the others | yes (round 10 what-if) | 7th and 8th note steal | low | `voices=6` |
| 4 | D4 | 1 to 2 points median; Stretched about 12 p99 (scaled) | yes (round 10 what-if) | thinner unison | low | `unison=2` |
| 5 | D7 | 0 factory; large for Serum imports with oversampling | no | more aliasing on those imports | low | cap `quality.oversample_4x` at tier low |
| 6 | D6 | 0.3 to 0.65 | yes (round 10 variants, upper bound) | level drop of masked effects | low | `fxMakeup` false at tier low |
| 7 | D5 | about 4.5 points on five presets | yes (round 12 section) | no chorus width | low | mask the chorus at tier low |
| 8 | D8 | 0 in the bench, small in playing | no | none at -90 dB; a ringing tail cut | medium | end a released voice below a level |


## 11. What I could not verify

- No device run: every saving that is not marked "measured" rests on the cycle model of section 1. S1, S4, S5, H1 and H2 are the least certain; S2, S3 and S7 have measured sections or a disassembled loop behind them.
- Shepard Ascent has no round 12 section run; its shares are round 10.
- The round 10 what-if runs (`configs.csv`, `variants.csv`) predate the round 11 and 12 kernels; D2, D4, D6 are scaled from them.
- The shell's per sample code was not disassembled from a current build: `fmaxf` as a call rests on the import table of the 2026-11-14 plugin, not on the loop of `output_peak.h` itself.
- The cost of `audioMasterGetTime` inside the host, and the host's own audio thread (11.5 % of core 3), cannot be read from this code.
- `synthyboy_profile_compare` was not run for any step down (D1 to D8); the sound statements are from `docs/MPC.md:524` or reasoning.
- `shell-mpc/kit/display_thread.h` does not exist in this tree; the display thread was read in `synthyboy_mpc.cpp`.
- The legato envelope cache note (section 9) is from reading only.
