# engine-own

The engine written for this project (original code, no Vital-derived content): the spike's 8-voice synth behind `synthyboy::IEngine`.
It is always built and the only engine of iOS builds (docs/ARCHITECTURE.md 2.2). It implements a small subset of the parameter
table; everything else is stored and read back (`getParam`) like in every engine but has no effect (`supportsParam` is false).

## Parameters the own engine acts on (`IEngine::supportsParam`)

| Key | What it does here |
|-----|-------------------|
| `master.volume` | master gain in dB (-60 dB and below = silence), ramped over 20 ms |
| `master.bend_range` | semitones of a full pitch bend |
| `master.transpose`, `master.tune` | pitch offset of NEW notes (semitones + cents) |
| `osc1.wavetable` | entry 0 (Sine) plays a sine, every other entry plays the band-limited saw (the table's entry 1 is the Saw) |
| `filter1.on`, `filter1.cutoff` | two cascaded one-pole low-pass filters (12 dB/oct); off = bypassed |

Event contract (state version 3, `capabilities () == kCapEventContract`): the sustain pedal (per channel) and the per-note tuning expression
act; the pressure and brightness expressions and the release velocity are accepted and have no destination without a modulation matrix. The
other capability bits (macros 5-8, LFO 9-10, aux sources, scale / tuning / MPE, oscillator mapping, ...) are not set: engine-own implements
none of them and `supportsParam` is false for those keys. Differences to engine-vital: no modulation, no tuning, no scale.

The voice has a fixed envelope (5 ms attack, 150 ms decay, sustain 0.7, 250 ms release), one oscillator, no unison, no modulation, no
effects. `Quality::maxVoices` is the only quality setting it uses (`setQuality` changes it while playing: voices above a lowered cap
are released, new notes take a voice below the cap). The generated list of what each parameter does in which engine is the column
"own engine" of docs/PARAMETERS.md.
