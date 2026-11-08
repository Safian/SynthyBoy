# LFOVOICE report - per-voice point modulation of custom LFO shapes, `lfoN.mono` for envelope / loop modes (2026-11-08)

Resolves the two engine items of the BLOCKED section of `WP123-REPORT.md` (items 1 and 2). Assumption A45 in `13-new-keys.md`. Upstream changes are listed in `docs/UPSTREAM.md` (two rows after the WP7 rows).

## 1. Per-voice point modulation

Before: one 2048 point table per LFO read by all voices; a point modulation by a per-voice source was stored and ignored.

Now (`engine-vital`):

* Vital clones the voice section once per voice pair (two voices = the four lanes of a `poly_float`). `SynthLfo` got one input per modulation source (`kPointSource0 + s`, 40 inputs) and, per pair, **one baked table per voice**. The four lanes read their voice's table through the per-lane pointer overload of `utils::getValueMatrix` (already in the imported code) and their own resolution. Without a point modulation `voice_mode_` is false and the code path is the old one (the default patch hash is unchanged: `version_2_sound_is_unchanged_in_both_engines` passes).
* The adapter (`adapter/vital_point_mod.h`, `CustomShapeModulation`) owns the shape, the point modulations (`content.lfo_shapes[].points[].mod_x / mod_y`, any source key) and bakes. A bake copies the unmodulated table and evaluates the shape again **only between the neighbours of every moved point** (the same `ShapeData::evaluate` as a full bake: the result is bit identical, tested on random shapes at 2048, 512 and 1000 points).
* The sources are plugged into the LFO like the aux source of a matrix slot (`applyPointModulation`): only the sources a shape reads, with the matrix's loop check (a source that depends on the LFO, e.g. its own output, stays unplugged: it would make the engine allocate a feedback node). Global sources (macros 1-8, mod wheel, pitch wheel, channel pressure) are not plugged; the adapter hands their value to all voices as before (the shared table is baked with them and with the per-voice sources at 0, and is the fallback of a voice whose table is not ready).
* **RT safety and bounded cost.** No allocation (tested with the counting `operator new`, all three tiers, 24 notes with envelope / velocity / poly aftertouch / LFO sources). A table is baked again only when a source it reads moved by at least 1/4096 (values are quantised to that step, which also makes the result independent of rounding noise). Per engine chunk (128 frames) the tier bounds the number of bakes: low 4 tables of 512 points, medium 8 and high 16 of 2048 points; a table that has waited 4 chunks is baked regardless. Worst case (all 10 LFOs, 33 voices, every source moving) is therefore `budget + tables / 4` bakes per chunk, each at most one table evaluation (about 2048 `evaluate` calls, or the part around the moved points).
* **Deterministic.** Same input gives bit-identical output (`point_modulation_is_deterministic`); the budget is counted per engine chunk, so a render is reproducible for the same host block size and events; a different block size changes the chunk grid and, only when the budget is exceeded, in which chunk a table is refreshed.
* **Quality tiers.** Resolution and budget come from `Quality::maxVoices` (<= 8 low, <= 16 medium); the stored parameters and the content are not changed.
* Capability bit `kCapCustomLfoShape` stays set; it is now true for every source.
* Found on the way: the old global path read macros 5-8 from the wrong source output (index of `velocity`); fixed (`modValue`).

Tests (`core/engine-vital/tests/test_lfovoice.cpp`): `velocity_moves_a_point_per_voice` (four notes in two voice pairs, loudness follows the velocity, swapped velocities swap it), `two_notes_in_turn_differ_by_velocity`, `envelope_state_moves_a_point_per_voice` (same time, different envelope state: 245.9 vs 8.8), `per_voice_x_and_y_modulation_and_global_sources_mix`, `point_modulation_is_deterministic`, `unmodulated_voice_tables_equal_the_shared_table` (bit exact), `incremental_bake_equals_a_full_bake`, allocation / tier test, own-output loop, content swaps while notes sound, content written on another thread.

## 2. `lfoN.mono` for the envelope and loop trigger types

The spec (13-new-keys.md, `lfoN.mono`) says "one LFO instance shared by all voices, phase continues across notes" and says nothing about the envelope / loop types, so the Serum-consistent behaviour is defined in **A45**: one instance per note group (a note on with no key and no pedal down starts a group and restarts the instance; later notes join it at its position; held while a key or the pedal is down; runs on after the last release, also for the voices in their release). Implemented as a master state in `SynthLfo` (`beginMono` / `endMono`): the first clone processed in a chunk advances it, the other voices start the chunk from it. The adapter derives the note group from the events (`keysHeld`, pedal channels) and counts the chunks (`runEngine`).

Limits, stated: the master is advanced with the rate / phase / delay / fade of the voice processed first in the chunk; the other voices read the shared position with their own rate for that chunk (matters only when the rate is key followed or voice-modulated).

Tests: `mono_envelope_lfo_is_one_instance_for_all_voices` (second note joins: 215.6 vs 6.2 polyphonic), `mono_envelope_lfo_restarts_with_a_new_note_group`, `mono_loop_lfo_shares_its_position`, `mono_sustain_envelope_lfo_is_held_while_any_key_is_down`, determinism / no allocation for Loop and hold.

## 3. Sanitizers

New and affected engine tests (`lfovoice`, `wp123`, `threads`, `vital_engine`): ASan + UBSan (`-fsanitize=address,undefined -fno-sanitize-recover=all`, RelWithDebInfo, separate build dir) and TSan (`-fsanitize=thread`): no finding, no change needed. (Apple clang has no LeakSanitizer; CI's Linux job runs it.)
